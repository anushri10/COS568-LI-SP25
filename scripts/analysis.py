import pandas as pd
import matplotlib.pyplot as plt
import os

def result_analysis():
    tasks  = ['fb', 'osmc', 'books']
    indexs = ['BTree', 'DynamicPGM', 'LIPP', 'HybridPGM']

    # Prepare data structures
    lookuponly_throughput     = {idx: {} for idx in indexs}
    insertlookup_throughput   = {idx: {'lookup': {}, 'insert': {}} for idx in indexs}
    insertlookup_mix1_throughput = {idx: {} for idx in indexs}
    insertlookup_mix2_throughput = {idx: {} for idx in indexs}

    for task in tasks:
        base = f"{task}_100M_public_uint64"
        # filenames
        f_lookup   = f"results/{base}_ops_2M_0.000000rq_0.500000nl_0.000000i_results_table.csv"
        f_ins50    = f"results/{base}_ops_2M_0.000000rq_0.500000nl_0.500000i_0m_results_table.csv"
        f_mix10    = f"results/{base}_ops_2M_0.000000rq_0.500000nl_0.100000i_0m_mix_results_table.csv"
        f_mix90    = f"results/{base}_ops_2M_0.000000rq_0.500000nl_0.900000i_0m_mix_results_table.csv"

        df_lookup = pd.read_csv(f_lookup)
        df_ins50  = pd.read_csv(f_ins50)
        df_mix10  = pd.read_csv(f_mix10)
        df_mix90  = pd.read_csv(f_mix90)

        for idx in indexs:
            # — lookup-only
            if idx == 'HybridPGM':
                filt = df_lookup['index_name'].str.contains('HybridPGM')
            else:
                filt = df_lookup['index_name'] == idx
            sub = df_lookup[filt]
            if not sub.empty:
                lookuponly_throughput[idx][task] = sub[
                    ['lookup_throughput_mops1','lookup_throughput_mops2','lookup_throughput_mops3']
                ].mean(axis=1).max()

            # — 50/50 insert+lookup
            if idx == 'HybridPGM':
                filt = df_ins50['index_name'].str.contains('HybridPGM')
            else:
                filt = df_ins50['index_name'] == idx
            sub = df_ins50[filt]
            if not sub.empty:
                insertlookup_throughput[idx]['lookup'][task] = sub[
                    ['lookup_throughput_mops1','lookup_throughput_mops2','lookup_throughput_mops3']
                ].mean(axis=1).max()
                insertlookup_throughput[idx]['insert'][task] = sub[
                    ['insert_throughput_mops1','insert_throughput_mops2','insert_throughput_mops3']
                ].mean(axis=1).max()

            # — mixed (10% insert)
            if idx == 'HybridPGM':
                filt = df_mix10['index_name'].str.contains('HybridPGM')
            else:
                filt = df_mix10['index_name'] == idx
            sub = df_mix10[filt]
            if not sub.empty:
                insertlookup_mix1_throughput[idx][task] = sub[
                    ['mixed_throughput_mops1','mixed_throughput_mops2','mixed_throughput_mops3']
                ].mean(axis=1).max()

            # — mixed (90% insert)
            if idx == 'HybridPGM':
                filt = df_mix90['index_name'].str.contains('HybridPGM')
            else:
                filt = df_mix90['index_name'] == idx
            sub = df_mix90[filt]
            if not sub.empty:
                insertlookup_mix2_throughput[idx][task] = sub[
                    ['mixed_throughput_mops1','mixed_throughput_mops2','mixed_throughput_mops3']
                ].mean(axis=1).max()

    # --- plotting (same as before, but now with HybridPGM) ---
    fig, axs = plt.subplots(2, 2, figsize=(12,10))
    axs = axs.flatten()
    bar_w = 0.2
    x = range(len(indexs))
    colors = ['tab:blue','tab:orange','tab:green','tab:red']

    # 1) lookup-only
    ax = axs[0]
    for i, t in enumerate(tasks):
        vals = [lookuponly_throughput[idx].get(t, 0) for idx in indexs]
        ax.bar([xi + i*bar_w for xi in x], vals, bar_w, label=t, color=colors[i])
    ax.set_title('Lookup-only');  ax.set_xticks([xi + bar_w*1.5 for xi in x]);  ax.set_xticklabels(indexs)
    ax.legend(), ax.set_ylabel('Mops/s')

    # 2) 50/50
    ax = axs[1]
    # lookups
    for i, t in enumerate(tasks):
        vals = [insertlookup_throughput[idx]['lookup'].get(t,0) for idx in indexs]
        ax.bar([xi + i*(bar_w/2) for xi in x], vals, bar_w/2, label=f'{t} (L)', color=colors[i])
    # inserts
    offset = bar_w*2
    for i, t in enumerate(tasks):
        vals = [insertlookup_throughput[idx]['insert'].get(t,0) for idx in indexs]
        ax.bar([xi + offset + i*(bar_w/2) for xi in x], vals, bar_w/2, label=f'{t} (I)', 
               color=colors[i], hatch='///')
    ax.set_title('50/50 Insert–Lookup');  ax.set_xticks([xi + bar_w*1.5 for xi in x]);  ax.set_xticklabels(indexs)
    ax.legend(), ax.set_ylabel('Mops/s')

    # 3) mixed (10% insert)
    ax = axs[2]
    for i, t in enumerate(tasks):
        vals = [insertlookup_mix1_throughput[idx].get(t,0) for idx in indexs]
        ax.bar([xi + i*bar_w for xi in x], vals, bar_w, label=t, color=colors[i])
    ax.set_title('Mixed (10% insert)');  ax.set_xticks([xi + bar_w*1.5 for xi in x]);  ax.set_xticklabels(indexs)
    ax.legend(), ax.set_ylabel('Mops/s')

    # 4) mixed (90% insert)
    ax = axs[3]
    for i, t in enumerate(tasks):
        vals = [insertlookup_mix2_throughput[idx].get(t,0) for idx in indexs]
        ax.bar([xi + i*bar_w for xi in x], vals, bar_w, label=t, color=colors[i])
    ax.set_title('Mixed (90% insert)');  ax.set_xticks([xi + bar_w*1.5 for xi in x]);  ax.set_xticklabels(indexs)
    ax.legend(), ax.set_ylabel('Mops/s')

    fig.suptitle('Throughput Comparison', fontsize=16)
    plt.tight_layout(rect=[0,0,1,0.95])
    plt.savefig('benchmark_results.png', dpi=300)
    plt.show()

    # Save CSVs for further use
    os.makedirs('analysis_results', exist_ok=True)
    pd.DataFrame(lookuponly_throughput).to_csv('analysis_results/lookuponly.csv')
    pd.DataFrame({idx: data['lookup'] for idx, data in insertlookup_throughput.items()})\
        .to_csv('analysis_results/ins50_lookup.csv')
    pd.DataFrame({idx: data['insert'] for idx, data in insertlookup_throughput.items()})\
        .to_csv('analysis_results/ins50_insert.csv')
    pd.DataFrame(insertlookup_mix1_throughput).to_csv('analysis_results/mix10.csv')
    pd.DataFrame(insertlookup_mix2_throughput).to_csv('analysis_results/mix90.csv')

if __name__ == "__main__":
    result_analysis()
