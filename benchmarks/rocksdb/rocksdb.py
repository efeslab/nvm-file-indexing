import matplotlib.pyplot as plt
import sys
import numpy as np
from matplotlib import rc
from matplotlib.figure import figaspect

rc('font',**{'size':'14','family':'serif','serif':['Palatino']})
rc('text', usetex=True)


add_average = False
xlabel_rotation = 90


def is_number(s):
    try:
        float(s)
        return True
    except ValueError:
        return False

filename='write'
if len(sys.argv) > 1:
  filename = sys.argv[1]

data = [[],{}]
with open(filename+'.txt', 'r') as data_file:
  is_first = True
  for line in data_file:
    if line[0] == '#':
        continue
    arr = line.strip().split()
    if is_first:
      for i in arr:
        data[0].append(i)
        data[1][i]=[]
      is_first = False
    else:
      for i in range(len(arr)):
        pushed_data=arr[i]
        if is_number(pushed_data):
          pushed_data=float(pushed_data)
        data[1][data[0][i]].append(pushed_data)

if add_average:
  for i in range(len(data[0])):
    key=data[0][i]
    if i == 0:
      val=r"{\bf Avg}"
    else:
      val=sum(data[1][key])/len(data[1][key])
    data[1][key].append(val)

w, h = figaspect(0.7/0.9)
fig, ax = plt.subplots(figsize=(w,h))

dim = len(data[0]) - 1
w = 0.75
dimw = w / dim

hatches=['/','.','x','\\','+']
colors=['#5BB8D7','#57A86B','#A8A857','#6E4587','#ADEBCC','#EBDCAD']
density=5
for i in range(len(hatches)):
  hatches[i]=hatches[i]*density
index=0


all_bars=[]

x=np.arange(len(data[1][data[0][0]]))
for i in range(1,len(data[0])):
  y = data[1][data[0][i]]
  b = ax.bar(x + (i-1) * dimw, y, dimw, label=data[0][i],zorder=3,fill=False,hatch=hatches[index],edgecolor=colors[index])
  index+=1
  all_bars.append(b)

#ax.set_xticks(x + dimw / 2, data[1][data[0][0]])
if dim == 1:
  plt.xticks(x, data[1][data[0][0]], rotation=xlabel_rotation)
else:
  plt.xticks(x+w/2-dimw/2, data[1][data[0][0]], rotation=xlabel_rotation)

if dim == 2:
  bar_modified=all_bars[0]
  bar_original=all_bars[1]
  for i in range(len(bar_modified)):
    difference = (( bar_original[i].get_height() - bar_modified[i].get_height()))# /bar_original[i].get_height())
    plt.text( (bar_modified[i].get_x())+0.175, bar_modified[i].get_height()+0.0, ''+str(round(difference, 1))+'', ha='center', va='bottom',rotation=45) #(bar_original[i].get_height() + bar_modified[i].get_height())/2 #,fontsize=17

#ax.set_xlabel('Workloads')
ax.set_ylabel(filename+'-bound cycles (\%)')
#ax.legend(ncol=1,columnspacing=0.25)
ax.grid(linestyle='--',zorder=0)
plt.axhline(0, color='gray')
plt.tight_layout()
fig.savefig(filename+".png",bbox_inches='tight',pad_inches=0)
