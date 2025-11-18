with open('Turn/TurnCorePhaseManager.cpp','r',encoding='utf-8',errors='ignore') as f:
    for idx,line in enumerate(f.readlines(),1):
        if 170<=idx<=210:
            print(f"{idx}: {line.rstrip()}")
