import io, re, sys

text = io.open('README.md', encoding='utf-8').read()

def anchor(title):
    a = title.strip().lower()
    a = re.sub(r'[^\w\s-]', '', a)
    a = re.sub(r'\s+', '-', a)
    return a

headings = [anchor(m.group(2)) for m in re.finditer(r'^(#{1,3})\s+(.*)$', text, re.M)]
links = re.findall(r'\]\(#([a-z0-9-]+)\)', text)
missing = [l for l in links if l not in headings]
print('headings:', len(headings), 'anchor links:', len(links))
print('broken anchors:', missing)

# Lokale Dateien, die verlinkt werden, muss es geben.
import os
files = set(re.findall(r'\]\((?!http|#)([^)#]+)\)', text))
fehlend = [f for f in files if not os.path.exists(f)]
print('local links:', sorted(files))
print('missing files:', fehlend)
sys.exit(0)
