import re,sys
dis=open(sys.argv[1]).read().split('\n')
funcs={}; cur=None
for l in dis:
    m=re.match(r'^([0-9a-f]+) <(.+)>:',l)
    if m: cur=m.group(2); funcs[cur]=[]; continue
    if cur: funcs[cur].append(l)
seen=set(); todo=['meLibOnProcess']
bad=0
while todo:
    f=todo.pop()
    if f in seen: continue
    if f not in funcs: print("MISSING",f); continue
    seen.add(f)
    for l in funcs[f]:
        m=re.search(r'\t(jal|j|bal)\s+[0-9a-f]+ <([^>+]+)',l)
        if m and m.group(2)!=f: todo.append(m.group(2))
        if re.search(r'\t(lv\.|sv\.|lv\.q|sv\.q|vmov|vadd|vmul|vdot|mtv|mfv|vcmp|syscall|jalr)',l) or re.search(r'\tjr\s+(?!ra)',l):
            print("CHECK", f, l.strip()); bad+=1
print("reached:", sorted(seen)); print("flagged:", bad)
