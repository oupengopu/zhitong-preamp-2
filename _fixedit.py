import sys

# === Fix 1: msgeq7.h ===
with open('PGA/msgeq7.h', 'r', encoding='utf-8') as f:
    lines = f.readlines()

target = '  memcpy(_smooth_l, new_sl, sizeof(_smooth_l));\n'
idx = next(i for i, l in enumerate(lines) if l == target)
print(f'msgeq7.h: insert after index {idx}')

new_lines = lines[:idx+1]
new_lines += [
    u'  // \u2014\u2014 \u8bca\u65ad\u65e5\u5fd7: \u6bcf 50 \u5e27 (~2.5s) \u8f93\u51fa\u4e00\u6b21\u9891\u6bb5\u5904\u7406\u7ed3\u679c \u2014\u2014\n'
]
new_lines += lines[idx+1:]

with open('PGA/msgeq7.h', 'w', encoding='utf-8') as f:
    f.writelines(new_lines)
print('msgeq7.h done')
