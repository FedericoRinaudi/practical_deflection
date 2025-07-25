#!/usr/bin/env python3
"""
Unisci 8 CSV seguendo la logica di zipping + join finale:

- I primi 6 file vengono letti e "zippati" in ordine:
  * dal primo prendo timestamp e la sua colonna valore
  * dai successivi 5 SOLO la seconda colonna (il valore), mantenendo l'ordine delle righe
  * nessuna join sui timestamp, nessuna eliminazione dei duplicati

- Gli ultimi 2 file vengono letti e "zippati" in ordine per ottenere:
  * timestamp e seq_num (dal primo dei due)
  * action (dal secondo dei due)

- Infine faccio una left join tra le due strutture su ['timestamp', 'seq_num'].
  Per le righe senza corrispondenza, 'action' = 2.

- Salvo il risultato finale su CSV.
"""

import argparse
import glob
import os
import pandas as pd


def first_csv_in(folder: str) -> str:
    pattern = os.path.join(folder, "*.csv")
    files = glob.glob(pattern)
    if not files:
        raise FileNotFoundError(f"Nessun file CSV trovato in {folder}")
    return files[0]


def zip_first_six(folders, names):
    """
    Restituisce un DataFrame con 1 + len(folders) colonne:
        timestamp, <name1>, <name2>, ..., <nameN>
    """
    # csv0: timestamp + primo valore
    csv0 = first_csv_in(folders[0])
    df = pd.read_csv(
        csv0,
        skiprows=1,
        header=None,
        names=['timestamp', names[0]],
        dtype={'timestamp': 'float64'}
    )

    # i successivi: solo colonna valore
    for folder, colname in zip(folders[1:], names[1:]):
        csv_path = first_csv_in(folder)
        col_df = pd.read_csv(
            csv_path,
            skiprows=1,
            header=None,
            usecols=[1],
            names=[colname]
        )
        df[colname] = col_df[colname].values[:len(df)]

    return df


def zip_last_two(folder_seq, seq_colname, folder_action, action_colname):
    """
    Restituisce un DataFrame con:
        timestamp, seq_num (seq_colname), action (action_colname)
    """
    # primo file: timestamp e seq_num
    csv_seq = first_csv_in(folder_seq)
    df = pd.read_csv(
        csv_seq,
        skiprows=1,
        header=None,
        usecols=[0, 1],
        names=['timestamp', seq_colname],
        dtype={'timestamp': 'float64'}
    )

    # secondo file: action
    csv_action = first_csv_in(folder_action)
    action_df = pd.read_csv(
        csv_action,
        skiprows=1,
        header=None,
        usecols=[1],
        names=[action_colname]
    )
    df[action_colname] = action_df[action_colname].values[:len(df)]

    return df


def main():
    parser = argparse.ArgumentParser(
        description="Unisci 8 CSV seguendo la logica di zipping + join finale"
    )
    # --- primi 6 ---
    parser.add_argument("--folder1", default="results_1G/QUEUE_CAPACITY")
    parser.add_argument("--name1",   default="capacity")
    parser.add_argument("--folder2", default="results_1G/QUEUES_TOT_CAPACITY")
    parser.add_argument("--name2",   default="total_capacity")
    parser.add_argument("--folder3", default="results_1G/QUEUE_LEN")
    parser.add_argument("--name3",   default="occupancy")
    parser.add_argument("--folder4", default="results_1G/QUEUES_TOT_LEN")
    parser.add_argument("--name4",   default="total_occupancy")
    parser.add_argument("--folder5", default="results_1G/SWITCH_SEQ_NUM")
    parser.add_argument("--name5",   default="seq_num")
    parser.add_argument("--folder6", default="results_1G/TTL")
    parser.add_argument("--name6",   default="ttl")

    # --- ultimi 2 ---
    parser.add_argument("--folder7", default="results_1G/ACTION_SEQ_NUM")
    parser.add_argument("--name7",   default="seq_num")
    parser.add_argument("--folder8", default="results_1G/PACKET_ACTION")
    parser.add_argument("--name8",   default="action")

    parser.add_argument("--output",   default="merged_final.csv")
    args = parser.parse_args()

    # 1) Zipping dei primi 6
    first_folders = [
        args.folder1, args.folder2, args.folder3,
        args.folder4, args.folder5, args.folder6
    ]
    first_names   = [
        args.name1, args.name2, args.name3,
        args.name4, args.name5, args.name6
    ]
    df_first = zip_first_six(first_folders, first_names)

    # 2) Zipping degli ultimi 2 -> timestamp, seq_num, action
    df_second = zip_last_two(
        args.folder7, args.name7,
        args.folder8, args.name8
    )

    # 3) Se necessario, allineo il nome seq_num
    join_keys = ['timestamp', args.name5]
    if args.name7 != args.name5:
        df_second = df_second.rename(columns={args.name7: args.name5})

    # 4) Log duplicati su ['timestamp', 'seq_num']
    dup1 = df_first[df_first.duplicated(join_keys, keep=False)]
    if not dup1.empty:
        dup1.to_csv("duplicates_first.csv", index=False)
        print("Duplicate entries in primo dataset salvate in duplicates_first.csv")
    else:
        print("Nessun duplicato trovato nel primo dataset")

    dup2 = df_second[df_second.duplicated(join_keys, keep=False)]
    if not dup2.empty:
        dup2.to_csv("duplicates_second.csv", index=False)
        print("Duplicate entries in secondo dataset salvate in duplicates_second.csv")
    else:
        print("Nessun duplicato trovato nel secondo dataset")

    # 5) Join finale su timestamp + seq_num
    merged = pd.merge(
        df_first,
        df_second[['timestamp', args.name5, args.name8]],
        on=join_keys,
        how='left'
    )
    merged[args.name8] = merged[args.name8].fillna(2).astype('int32')

    merged.to_csv(args.output, index=False)
    print(f"Dati uniti salvati in {args.output}")


if __name__ == '__main__':
    main()
