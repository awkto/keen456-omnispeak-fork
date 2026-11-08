import csv,statistics as s,sys
for f in sys.argv[1:]:
    r=list(csv.DictReader(open(f)))
    c=[(int(x['sim']),int(x['cam_x_qpx'])) for x in r]
    d=[abs(c[i+1][1]-c[i][1])/4 for i in range(len(c)-1) if 140<=c[i][0]<255]
    print(f, 'n=%d stddev=%.3f max=%.2f frozen=%.0f%%'%(len(d),s.pstdev(d),max(d),100*sum(1 for x in d if x==0)/len(d)))
