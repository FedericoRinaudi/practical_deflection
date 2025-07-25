import csv
import sys

def find_duplicates(filename):
    """
    Apre un file CSV, scarta la prima riga (header),
    identifica duplicati nella seconda colonna e ne conta le occorrenze.
    Se non ci sono duplicati, stampa "nessun duplicati".
    """
    counts = {}  # dizionario che mappa valore → numero di occorrenze

    with open(filename, newline='', encoding='utf-8') as csvfile:
        reader = csv.reader(csvfile)
        # Scarta la prima riga (header)
        try:
            next(reader)
        except StopIteration:
            # File vuoto o solo header
            pass

        # Conta tutte le occorrenze nella seconda colonna
        for row in reader:
            if len(row) < 2:
                continue
            value = row[1]
            counts[value] = counts.get(value, 0) + 1

    # Filtra solo i valori con più di 1 occorrenza
    duplicates = {val: cnt for val, cnt in counts.items() if cnt > 1}

    # Stampa risultati
    if duplicates:
        print("Duplicati trovati:")
        for val, cnt in sorted(duplicates.items()):
            print(f"{val}: {cnt} occorrenze")
    print(len(duplicates), "duplicati trovati" if duplicates else "nessun duplicati")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Uso: python {sys.argv[0]} <file.csv>")
        sys.exit(1)
    find_duplicates(sys.argv[1])