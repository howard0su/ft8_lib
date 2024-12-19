#!/usr/bin/env python3

import sys, os, subprocess
from concurrent.futures import ThreadPoolExecutor

def parse(line):
    fields = line.strip().split()
    freq = fields[3]
    dest = fields[5] if len(fields) > 5 else ''
    source = fields[6] if len(fields) > 6 else ''
    report = fields[7] if len(fields) > 7 else ''
    if dest and dest[0] == '<' and dest[-1] == '>':
        dest = '<...>'
    if source and source[0] == '<' and source[-1] == '>':
        source = '<...>'
    return ' '.join([dest, source, report])

def recursive_listdir(wav_dir):
    items = []
    for root, dirs, files in os.walk(wav_dir):
        # Combine directory and files to mimic os.listdir
        items.extend([os.path.join(root, d) for d in dirs])
        items.extend([os.path.join(root, f) for f in files])
    return items

wav_dir = sys.argv[1]
wav_files = recursive_listdir(wav_dir)
wav_files = [f for f in wav_files if os.path.isfile(f) and os.path.splitext(f)[1] == '.wav']
txt_files = [os.path.splitext(f)[0] + '.txt' for f in wav_files]

is_ft4 = False
if len(sys.argv) > 2 and sys.argv[2] == '-ft4':
    is_ft4 = True

def process_file(wav_file, txt_file, is_ft4):
    if not os.path.isfile(txt_file):
        return (0, 0, 0)  # n_extra, n_missed, n_total

    cmd_args = ['./decode_ft8', wav_file]
    if is_ft4:
        cmd_args.append('-ft4')

    result = subprocess.run(cmd_args, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    result = result.stdout.decode('utf-8').split('\n')
    result = [parse(x) for x in result if len(x) > 0]
    result = set(result)

    expected = open(txt_file).read().split('\n')
    expected = [parse(x) for x in expected if len(x) > 0]
    expected = set(expected)

    extra_decodes = result - expected
    missed_decodes = expected - result

    return len(extra_decodes), len(missed_decodes), len(expected), wav_file, extra_decodes, missed_decodes

n_extra = 0
n_missed = 0
n_total = 0
with ThreadPoolExecutor() as executor:
    futures = [executor.submit(process_file, wav_file, txt_file, is_ft4) 
                for wav_file, txt_file in zip(wav_files, txt_files)]

    for future in futures:
        extra, missed, total, wav_file, extra_decodes, missed_decodes = future.result()
        n_extra += extra
        n_missed += missed
        n_total += total    

        print(wav_file)
        if len(extra_decodes) > 0:
            print('Extra decodes: ', list(extra_decodes))
        if len(missed_decodes) > 0:
            print('Missed decodes: ', list(missed_decodes))

print('Total: %d, extra: %d (%.1f%%), missed: %d (%.1f%%)' % 
        (n_total, n_extra, 100.0*n_extra/n_total, n_missed, 100.0*n_missed/n_total))
recall = (n_total - n_missed) / float(n_total)
print('Recall: %.1f%%' % (100*recall, ))

precision = (n_total - n_extra) / float(n_total + n_extra)  # or adjust based on definitions
print('Precision: %.1f%%' % (100 * precision,))
f1_score = 2 * (precision * recall) / (precision + recall)
print('F1 Score: %.1f%%' % (100 * f1_score,))

print('%d (%.1f%%)|%d (%.1f%%)|%.1f%%|%.1f%%|%.1f%%' % (n_extra, 100.0*n_extra/n_total, n_missed, 100.0*n_missed/n_total, 100 * recall, 100 * precision, 100 * f1_score))
