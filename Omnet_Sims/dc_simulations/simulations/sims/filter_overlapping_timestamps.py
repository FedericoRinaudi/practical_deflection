#!/usr/bin/env python3
import argparse
import os
import glob
import numpy as np
import pandas as pd


def find_csv(folder: str) -> str:
    pattern = os.path.join(folder, "*.csv")
    files = glob.glob(pattern)
    if not files:
        raise FileNotFoundError(f"Nessun file CSV trovato in {folder}")
    files.sort()
    return files[0]


def load_df_standard(path):
    return pd.read_csv(
        path,
        header=None,
        skiprows=1,
        names=['timestamp', 'seq_num'],
        dtype={'timestamp': 'float64', 'seq_num': 'int64'}
    )


def load_4_file(path):
    df = pd.read_csv(
        path,
        header=None,
        skiprows=1,
        names=["time","capacity","total_capacity",
               "occupancy","total_occupancy",
               "seq_num","ttl","action"],
        dtype={'time': 'float64', 'capacity': 'int64',
               'total_capacity': 'int64', 'occupancy': 'int64',
               'total_occupancy': 'int64', 'seq_num': 'int64',
               'ttl': 'int64', 'action': 'int64'}
    )
    if df.empty:
        raise ValueError(f"Il file {path} è vuoto o non contiene dati validi.")
    return df


def save_df(path, df):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    df.to_csv(path, index=False, header=True)


def build_intervals(df1: pd.DataFrame, df2: pd.DataFrame):
    """
    Ricostruisce gli intervalli (start_time, end_time) per ogni seq_num.
    Mantiene esattamente la stessa logica del codice originale.
    Restituisce interval_df e l'insieme to_remove.
    """
    # Conteggi e rimozioni iniziali (vettoriale)
    vc1 = df1['seq_num'].value_counts()
    vc2 = df2['seq_num'].value_counts()
    aligned_vc2 = vc2.reindex(vc1.index).fillna(0).astype('int64')
    to_remove = set(vc1.index[(vc1 > 1) & (vc1 > aligned_vc2 + 1)])

    # Pre-ordino e raggruppo una volta sola
    df1s = df1.sort_values(['seq_num', 'timestamp'], kind='mergesort')
    df2s = df2.sort_values(['seq_num', 'timestamp'], kind='mergesort')

    starts_grp = df1s.groupby('seq_num', sort=False)['timestamp']
    ends_grp   = df2s.groupby('seq_num', sort=False)['timestamp']

    # Materializzo in array NumPy (accesso O(1) per seq)
    starts_map = {k: v.to_numpy(copy=False) for k, v in starts_grp}
    ends_map   = {k: v.to_numpy(copy=False) for k, v in ends_grp}

    all_seqs = set(starts_map.keys()) | set(ends_map.keys())

    out_seq = []
    out_start = []
    out_end = []

    # Loop per-seq efficiente (niente .loc ripetuti)
    for seq in all_seqs:
        if seq in to_remove:
            continue

        s = starts_map.get(seq, np.array([], dtype='float64'))
        e = ends_map.get(seq, np.array([], dtype='float64'))
        ns, ne = s.size, e.size

        if ns == 0 or ne == 0:
            to_remove.add(seq)
            continue

        #Qua prova poi a ricostruire (laddove possibile) con ttl, ma per ora scarta
        # Casi come nell'originale
        #if ns > 1 and ne == 1:
        #    # multiple start, single end
        #    out_seq.extend([seq] * ns)
        #    out_start.extend(s.tolist())
        #    out_end.extend([e[0]] * ns)

        elif ns == 1 and ne == 1:
            # single start, single end
            out_seq.append(seq)
            out_start.append(s[0])
            out_end.append(e[0])

        elif ns > 1 and ne > 1 and ns == ne:
            # multiple start e multiple end con stesso numero di occorrenze
            # Stessa condizione dell'originale: e[i] < s[i+1] per tutti i prefissi
            if np.all(e[:-1] < s[1:]):
                out_seq.extend([seq] * ns)
                out_start.extend(s.tolist())
                out_end.extend(e.tolist())
            else:
                to_remove.add(seq)
        else:
            # qualsiasi altro caso è ambiguo
            to_remove.add(seq)

    interval_df = pd.DataFrame(
        {
            'seq_num': np.array(out_seq, dtype='int64'),
            'start_time': np.array(out_start, dtype='float64'),
            'end_time': np.array(out_end, dtype='float64'),
        },
        columns=['seq_num', 'start_time', 'end_time']
    )
    return interval_df, to_remove


def main():
    parser = argparse.ArgumentParser(
        description='Filtra CSV basati su occorrenze di seq_num e unisce i primi due.'
    )
    parser.add_argument('--dir1', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/SND_TS_SEQ_NUM',
                        help='Cartella con il primo CSV (time, seq_num)')
    parser.add_argument('--dir2', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/RCV_TS_SEQ_NUM',
                        help='Cartella con il secondo CSV (time, seq_num)')
    parser.add_argument('--dir3', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/OOO_SEG',
                        help='Cartella con il terzo CSV')
    parser.add_argument('--file4', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/merged_final.csv',
                        help='Percorso al quinto CSV')
    args = parser.parse_args()

    # Trova i file CSV
    path1 = find_csv(args.dir1)
    path2 = find_csv(args.dir2)
    path3 = find_csv(args.dir3)
    path4 = args.file4

    # Lettura dei primi due file
    df1 = load_df_standard(path1)
    df2 = load_df_standard(path2)

    # Costruzione intervalli (ottimizzata)
    interval_df, to_remove = build_intervals(df1, df2)

    # Terzo file
    df3 = load_df_standard(path3)
    df3_f = df3[~df3['seq_num'].isin(to_remove)]

    # Verifica: ogni riga di df3_f deve matchare una e UNA sola riga di interval_df
    merged3 = df3_f.merge(
        interval_df,
        left_on=['seq_num', 'timestamp'],
        right_on=['seq_num', 'end_time'],
        how='left',
        indicator=True
    )
    if merged3['_merge'].eq('both').sum() != len(df3_f):
        raise ValueError("Alcune righe di df3_f non trovano corrispondenza unica in interval_df.")

    # 'ooo' vettoriale: 1 se (seq_num, end_time) compare in df3_f
    matches = merged3.loc[merged3['_merge'] == 'both', ['seq_num', 'end_time']].drop_duplicates()
    interval_df = interval_df.merge(
        matches.assign(ooo=1),
        on=['seq_num', 'end_time'],
        how='left'
    )
    interval_df['ooo'] = interval_df['ooo'].fillna(0).astype('int8')

    # Quinto file
    df4 = load_4_file(path4)
    df4_f = df4[~df4['seq_num'].isin(to_remove)]

    # Join finale (stessa semantica: join su seq_num e filtro per time dentro all'intervallo)
    merged = pd.merge(df4_f, interval_df, on='seq_num', how='left')
    final_ds = merged[(merged['time'] > merged['start_time']) & (merged['time'] < merged['end_time'])]

    save_df('results_1G/final_dataset.csv', final_ds)


if __name__ == '__main__':
    main()
