#!/usr/bin/env python3
"""
Script per filtrare file CSV in base alle occorrenze di `seq_num` in cinque file e unire i primi due.

- Conta le occorrenze di ogni `seq_num` in `file1` e `file2`.
- Seleziona i `seq_num` con occorrenze >1 in `file1` e tali per cui:
  `count_file1 > count_file2 + 1`.
- Unisce i primi due file in base alle regole:
  * Se un `seq_num` ha più occorrenze in file1 ma solo una in file2,
    assegna quell'unico valore di receive_time a tutti i send_time.
  * Se entrambi hanno più occorrenze:
    prova ad accoppiare ordinando start/end e verificando che il più piccolo end_time
    sia maggiore del secondo più piccolo start_time; se sì, definisci l'intervallo,
    altrimenti marca come ambiguo.
  * Gli altri casi (assenza di end o mismatch) sono considerati ambigui.
- Rimuove tutte le righe contenenti i `seq_num` ambigui da tutti i file.
- Salva l'unione dei primi due file in un nuovo CSV.
- Alla fine, genera un CSV con tutti i `seq_num` eliminati.

Per i primi 4 input si passa la cartella contenente un unico file .csv; per il quinto si passa il file diretto.
"""
import argparse
import os
import glob
import pandas as pd


def find_csv(folder: str) -> str:
    pattern = os.path.join(folder, "*.csv")
    files = glob.glob(pattern)
    if not files:
        raise FileNotFoundError(f"Nessun file CSV trovato in {folder}")
    return files[0]


def load_df_standard(path):
    df = pd.read_csv(path, header=None, skiprows=1,
                     names=['timestamp', 'seq_num'],
                     dtype={'timestamp': 'float64', 'seq_num': 'int64'})
    return df


def load_5_file(path):
    df = pd.read_csv(path, header=None, skiprows=1,
                     names=["timestamp","capacity","total_capacity",
                            "occupancy","total_occupancy",
                            "seq_num","ttl","action"],
                     dtype={'timestamp': 'float64', 'capacity': 'int64',
                            'total_capacity': 'int64', 'occupancy': 'int64',
                            'total_occupancy': 'int64', 'seq_num': 'int64',
                            'ttl': 'int64', 'action': 'int64'})
    if df.empty:
        raise ValueError(f"Il file {path} è vuoto o non contiene dati validi.")
    return df


def save_df(path, df):
    df.to_csv(path, index=False, header=True)


def main():
    parser = argparse.ArgumentParser(
        description='Filtra CSV basati su occorrenze di seq_num e unisce i primi due.')
    parser.add_argument('--dir1', help='Cartella con il primo CSV (time, seq_num)')
    parser.add_argument('--dir2', help='Cartella con il secondo CSV (time, seq_num)')
    parser.add_argument('--dir3', help='Cartella con il terzo CSV')
    parser.add_argument('--dir4', help='Cartella con il quarto CSV')
    parser.add_argument('--file5', help='Percorso al quinto CSV')
    args = parser.parse_args()

    # Trova i file CSV
    path1 = find_csv(args.dir1)
    path2 = find_csv(args.dir2)
    path3 = find_csv(args.dir3)
    path4 = find_csv(args.dir4)
    path5 = args.file5

    # Lettura dei primi due file
    df1 = load_df_standard(path1)
    df2 = load_df_standard(path2)

    # Conteggio occorrenze seq_num
    vc1 = df1['seq_num'].value_counts()
    vc2 = df2['seq_num'].value_counts()

    # Seleziona seq_num da rimuovere prima dell'unione
    to_remove = set(
        seq for seq, c1 in vc1.items()
        if c1 > 1 and c1 > vc2.get(seq, 0) + 1
    )

    # Unione dei primi due file
    merged_rows = []
    all_seqs = set(vc1.index).union(vc2.index)

    for seq in sorted(all_seqs):
        if seq in to_remove:
            continue
        s_times = sorted(df1.loc[df1['seq_num'] == seq, 'timestamp'].tolist())
        e_times = sorted(df2.loc[df2['seq_num'] == seq, 'timestamp'].tolist())
        ns, ne = len(s_times), len(e_times)

        # Caso multiple start, single end
        if ns > 1 and ne == 1:
            e = e_times[0]
            for s in s_times:
                merged_rows.append({'seq_num': seq, 'start_time': s, 'end_time': e})
        # Caso single start, single end
        elif ns == 1 and ne == 1:
            merged_rows.append({'seq_num': seq, 'start_time': s_times[0], 'end_time': e_times[0]})
        # Caso multiple start e multiple end, stesso numero di occorrenze
        elif ns > 1 and ne > 1 and ns == ne:
            st = s_times.copy()
            et = e_times.copy()
            ok = True
            pairs = []
            while st:
                # Controllo univocità: primo end > secondo start
                if len(st) >= 2 and et[0] > st[1]:
                    pairs.append((st[0], et[0]))
                    st.pop(0)
                    et.pop(0)
                else:
                    ok = False
                    break
            if ok:
                for s, e in pairs:
                    merged_rows.append({'seq_num': seq, 'start_time': s, 'end_time': e})
            else:
                to_remove.add(seq)
        else:
            # Qualsiasi altro caso è ambiguo
            to_remove.add(seq)

    # Salva l'unione in nuovo CSV
    merged_df = pd.DataFrame(merged_rows, columns=['seq_num', 'start_time', 'end_time'])
    merged_path = os.path.join(os.path.dirname(path1), 'merged_1_2.csv')
    merged_df.to_csv(merged_path, index=False)

    # Filtra tutti i file usando la lista aggiornata di to_remove
    def filter_and_save(path, loader):
        df = loader(path)
        df_f = df[~df['seq_num'].isin(to_remove)]
        save_df(path, df_f)

    filter_and_save(path1, load_df_standard)
    filter_and_save(path2, load_df_standard)
    filter_and_save(path3, load_df_standard)
    filter_and_save(path4, load_df_standard)

    # Per il quinto file
    df5 = load_5_file(path5)
    df5_f = df5[~df5['seq_num'].isin(to_remove)]
    save_df(path5, df5_f)

    # Salva seq_num rimossi
    removed_df = pd.DataFrame({'seq_num': sorted(to_remove)})
    out_removed = os.path.join(os.path.dirname(path1), 'removed_seq_nums.csv')
    removed_df.to_csv(out_removed, index=False)

    print(f"Unione salvata in: {merged_path}")
    print(f"Seq_num rimossi salvati in: {out_removed}")


if __name__ == '__main__':
    main()
