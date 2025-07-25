#!/usr/bin/env python3
"""
Script per filtrare file CSV in base alle occorrenze di `seq_num` in due file principali e rimuovere queste sequenze da tutti i CSV passati.

- Conta le occorrenze di ogni `seq_num` in `file1` e `file2`.
- Seleziona i `seq_num` con occorrenze >1 in `file1` e tali per cui:
  `count_file1 > count_file2 + 1`.
- Rimuove tutte le righe contenenti questi `seq_num` da `file1` e `file2`.
- Per i file nelle prime quattro cartelle, usa il path della cartella per trovare il file CSV.
- Per il quinto input, usa direttamente il file CSV.
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


def load_df_statndard(path):
    df = pd.read_csv(path, header=None, skiprows=1, names=['timestamp', 'seq_num'], 
                     dtype={'timestamp': 'float64', 'seq_num': 'int64'})
    return df

def load_5_file(path):
    df = pd.read_csv(path, header=None, skiprows=1, names=["timestamp","capacity","total_capacity","occupancy","total_occupancy","seq_num","ttl","action"],
                     dtype={'timestamp': 'float64', 'capacity': 'int64', 'total_capacity': 'int64', 
                            'occupancy': 'int64', 'total_occupancy': 'int64', 'seq_num': 'int64', 'ttl': 'int64', 'action': 'int64'})
    if df.empty:
        raise ValueError(f"Il file {path} è vuoto o non contiene dati validi.")
    return df


def save_df(path, df):
    df.to_csv(path, index=False, header=True)


def main():
    parser = argparse.ArgumentParser(
        description='Filtra CSV basati su occorrenze di seq_num.')
    parser.add_argument('--dir1', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/SND_TS_SEQ_NUM',
                        help='Cartella con il primo CSV (time, seq_num)')
    parser.add_argument('--dir2', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/RETRANSMITTED',
                        help='Cartella con il secondo CSV (time, seq_num)')
    parser.add_argument('--dir3', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/OOO_SEG',
                        help='Cartella con il terzo CSV')
    parser.add_argument('--dir4', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/RCV_TS_SEQ_NUM',
                        help='Cartella con il quarto CSV')
    parser.add_argument('--file5', default='/home/ubuntu/pd/Omnet_Sims/dc_simulations/simulations/sims/results_1G/merged_final.csv',
                        help='Percorso al quinto CSV')

    args = parser.parse_args()

    # Trova i file CSV nelle prime quattro cartelle
    path1 = find_csv(args.dir1)
    path2 = find_csv(args.dir2)
    path3 = find_csv(args.dir3)
    path4 = find_csv(args.dir4)
    path5 = args.file5

    # Lettura dei primi due file (seq_num in colonna 1)
    df1 = load_df_statndard(path1)
    df2 = load_df_statndard(path2)

    # Conteggio occorrenze seq_num (colonna 1)
    vc1 = df1['seq_num'].value_counts()
    vc2 = df2['seq_num'].value_counts()

    # Seleziona seq_num da rimuovere
    to_remove = [seq for seq, cnt in vc1.items()
                 if cnt > 1 and cnt > vc2.get(seq, 0) + 1]

    # Filtra i primi due file
    df1_filtered = df1[~df1['seq_num'].isin(to_remove)]
    df2_filtered = df2[~df2['seq_num'].isin(to_remove)]
    save_df(path1, df1_filtered)
    save_df(path2, df2_filtered)

    df3 = load_df_statndard(path3)  # Carica il terzo file
    df3_filtered = df3[~df3['seq_num'].isin(to_remove)]
    save_df(path3, df3_filtered)
    
    df4 = load_df_statndard(path4)  # Carica il quarto file
    df4_filtered = df4[~df4['seq_num'].isin(to_remove)]
    save_df(path4, df4_filtered)
    
    df5 = load_5_file(path5)  # Carica il quinto file
    df5_filtered = df5[~df5['seq_num'].isin(to_remove)]
    save_df(path5, df5_filtered)
    

    # Salva seq_num rimossi
    removed_df = pd.DataFrame({'seq_num': to_remove})
    removed_df['seq_num'] = removed_df['seq_num'].astype(int)
    removed_df.to_csv('results_1G/removed_seq_nums.csv', index=False)


if __name__ == '__main__':
    main()
