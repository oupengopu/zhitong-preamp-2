import os
os.chdir("E:\\claude\\zhitong-preamp-2-master")

with open("PGA/msgeq7.h", "rb") as f:
    data = f.read()

# Find _diag_cnt section and remove it
idx = data.find(b"static int _diag_cnt = 0;")
if idx > 0:
    start = data.rfind(b"\n", 0, idx)  # start of the diagnostic block
    # Find the closing } after the block - use CRLF aware search
    end_marker = b"}\r\n\r\n//"  # in CRLF file
    end = data.find(end_marker, idx)
    if end > 0:
        end = end + 1  # include the }
        data = data[:start] + data[end:]
    else:
        # Try without CRLF
        end = data.find(b"}\n\n//", idx)
        if end > 0:
            end = end + 1
            data = data[:start] + data[end:]
        else:
            print("ERROR: could not find end marker")

with open("PGA/msgeq7.h", "wb") as f:
    f.write(data)
print("msgeq7.h done")

# Verify
lines = open("PGA/msgeq7.h", "rb").readlines()
diag = [i+1 for i,l in enumerate(lines) if b"_diag_cnt" in l]
print(f"Remaining _diag_cnt lines: {diag}" if diag else "All clean!")
