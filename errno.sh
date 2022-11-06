sed -En 's/^\[([^]]+)\].*$/case \1: return "\1";/p' errno.txt
