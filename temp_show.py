import io
with open('Turn/TurnCorePhaseManager.cpp','r',encoding='utf-8',errors='ignore') as f:
    lines=f.readlines()
for idx,line in enumerate(lines,1):
    if 440<=idx<=520 or 640<=idx<=700:
        print(f"{idx}: {line.rstrip()}" )
