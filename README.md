dog: a tool for converting all your Sakuya pics to OpenComputers

uses the CTIF format: https://github.com/ChenThread/ctif

for actual readme see dog.c

this file merely contains a shitty maze generator

    print "#"*79 + "\n#" + 77*" " + "#\n# " + "#\n# ".join("#\n# ".join("".join(k) for k in zip(*((("# ", "# ") if __import__("random").random() > 0.5 else ("##","  ")) for i in xrange(38)))) for j in xrange(11)) + "#\n" + "#"*79

