import json,collections,sys
d=json.load(open(sys.argv[1])); ev=d['traceEvents'] if isinstance(d,dict) else d
profiles=collections.defaultdict(lambda:{'nodes':{},'samples':[],'deltas':[]})
tid_of={}
for e in ev:
    if e.get('name')=='Profile': tid_of[e['id']]=(e['pid'],e['tid'])
    if e.get('name')=='ProfileChunk':
        p=profiles[(e['pid'],e['id'])]
        a=e['args']['data']
        cp=a.get('cpuProfile',{})
        for n in cp.get('nodes',[]): p['nodes'][n['id']]=n
        p['samples']+=cp.get('samples',[]); p['deltas']+=a.get('timeDeltas',[])
for k,p in profiles.items():
    nodes=p['nodes']; parent={}
    for n in nodes.values():
        if 'parent' in n: parent[n['id']]=n['parent']
    tot=sum(p['deltas'])/1000
    if tot<500: continue
    selfc=collections.Counter(); incl=collections.Counter()
    for s,dt in zip(p['samples'],p['deltas']):
        n=nodes.get(s)
        if not n: continue
        f=n['callFrame']; selfc[f['functionName'] or f.get('url','')[-30:] or '(anon)']+=dt
        seen=set(); x=s
        while x in nodes:
            nm=nodes[x]['callFrame']['functionName']
            if nm not in seen: incl[nm]+=dt; seen.add(nm)
            x=parent.get(x)
            if x is None: break
    T=sum(selfc.values())
    print('== profile',k,'thread',tid_of.get(k[1]),'ms',round(tot))
    for nm,v in selfc.most_common(int(sys.argv[2]) if len(sys.argv)>2 else 40): print(f'{100*v/T:5.1f}%  {nm[:110]}')
    print('-- inclusive')
    for nm,v in incl.most_common(int(sys.argv[3]) if len(sys.argv)>3 else 40): print(f'{100*v/T:5.1f}%  {nm[:110]}')
