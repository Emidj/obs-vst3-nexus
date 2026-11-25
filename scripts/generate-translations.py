from pathlib import Path
import json,re,sys
root=Path(__file__).resolve().parent.parent
rows=[s.split('|') for s in (root/'src/i18n/translations.tsv').read_text(encoding='utf-8-sig').splitlines() if s]
assert all(len(row)==5 and all(row) for row in rows), 'Each key requires five translations'
assert len({row[0] for row in rows})==len(rows), 'Duplicate translation key'
for row in rows:
 assert all(sorted(re.findall(r'%\d',value))==sorted(re.findall(r'%\d',row[0])) for value in row), 'Placeholder mismatch'
outputs={root/'src/i18n/translations.json':json.dumps({'languages':['en','es','it','fr','de'],'strings':rows},ensure_ascii=False,indent=2)}
for name,begin,end,render in [
 ('src/i18n/strings.hpp','static const char* rows[][5]={\n','\n};\nfor(',lambda row:'{'+','.join('u8'+json.dumps(value,ensure_ascii=True) for value in row)+'}'),
 ('installer/Languages.cs','internal static readonly string[][] Rows={\n','\n };\n internal static string Text',lambda row:'new string[]{'+','.join(json.dumps(value,ensure_ascii=True) for value in row)+'}')]:
 path=root/name;text=path.read_text(encoding='utf-8');a=text.index(begin)+len(begin);b=text.index(end,a)
 outputs[path]=text[:a]+',\n'.join(render(row) for row in rows)+text[b:]
for path,output in outputs.items():
 if '--check' in sys.argv:assert path.read_text(encoding='utf-8')==output, 'Regenerate '+str(path)
 else:path.write_text(output,encoding='utf-8')
print(str(len(rows))+' keys, five complete languages, placeholders and generated files verified')
