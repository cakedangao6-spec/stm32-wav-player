#!/usr/bin/env python3
"""把 git diff 输出转换成带折叠的 HTML diff 视图"""
import subprocess, json, sys, os, re
from datetime import datetime

def run_git_diff(path):
    """获取 git diff 的 unified 格式（3行上下文）"""
    result = subprocess.run(
        ['git', 'diff', '--unified=3'],
        cwd=path, capture_output=True, text=True
    )
    return result.stdout

def parse_diff(diff_text):
    """解析 unified diff 成结构化数据"""
    files = []
    current_file = None
    current_lines = []
    old_num = 0
    new_num = 0
    
    for line in diff_text.split('\n'):
        if line.startswith('diff --git'):
            if current_file and current_lines:
                current_file['lines'] = current_lines
                files.append(current_file)
            parts = line.split()
            fname = parts[-1].replace('b/', '', 1) if len(parts) >= 4 else 'unknown'
            current_file = {'name': fname}
            current_lines = []
            old_num = 0
            new_num = 0
        elif line.startswith('@@'):
            m = re.search(r'-(\d+).*\+(\d+)', line)
            if m:
                old_num = int(m.group(1))
                new_num = int(m.group(2))
            current_lines.append({'type': 'hunk', 'code': line})
        elif line.startswith('---') or line.startswith('+++'):
            continue
        elif line.startswith('-'):
            current_lines.append({'type': 'del', 'old_num': str(old_num), 'new_num': '', 'code': line[1:]})
            old_num += 1
        elif line.startswith('+'):
            current_lines.append({'type': 'add', 'old_num': '', 'new_num': str(new_num), 'code': line[1:]})
            new_num += 1
        elif line.startswith('\\'):
            current_lines.append({'type': 'ctx', 'old_num': '', 'new_num': '', 'code': line})
        else:
            current_lines.append({'type': 'ctx', 'old_num': str(old_num), 'new_num': str(new_num), 'code': line})
            old_num += 1
            new_num += 1
    
    if current_file and current_lines:
        current_file['lines'] = current_lines
        files.append(current_file)
    
    return files

def generate_html(files, title="代码差异审阅"):
    """生成带折叠的 HTML diff 视图"""
    total_add = sum(1 for f in files for l in f['lines'] if l['type'] == 'add')
    total_del = sum(1 for f in files for l in f['lines'] if l['type'] == 'del')
    
    html = f'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>{title}</title>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{
    font-family: 'Cascadia Code', 'Fira Code', 'Consolas', monospace;
    background: #0d1117; color: #c9d1d9; padding: 16px; font-size: 14px; line-height: 1.6;
  }}
  .header {{ margin-bottom: 16px; padding-bottom: 12px; border-bottom: 1px solid #30363d; }}
  .header h1 {{ font-size: 18px; font-weight: 600; color: #e6edf3; }}
  .header .meta {{ color: #8b949e; font-size: 13px; margin-top: 4px; }}
  .header .meta span {{ margin-right: 16px; }}
  .add-badge {{ color: #3fb950; }}
  .del-badge {{ color: #f85149; }}
  
  .file-section {{ margin-bottom: 8px; border: 1px solid #30363d; border-radius: 6px; overflow: hidden; }}
  .file-header {{
    background: #161b22; padding: 8px 12px; font-weight: 600; color: #e6edf3;
    cursor: pointer; display: flex; justify-content: space-between; user-select: none;
  }}
  .file-header:hover {{ background: #1c2333; }}
  .file-header .stats {{ font-weight: 400; font-size: 12px; }}
  .file-header .stats .add {{ color: #3fb950; }}
  .file-header .stats .del {{ color: #f85149; }}
  .file-header .arrow {{ color: #8b949e; margin-right: 8px; }}
  .file-content {{ display: none; }}
  .file-content.open {{ display: block; }}
  .diff-table {{ width: 100%; border-collapse: collapse; }}
  .diff-table tr {{ border-bottom: 1px solid #21262d; }}
  .diff-table tr:last-child {{ border-bottom: none; }}
  .diff-table td {{ padding: 0 8px; white-space: pre; vertical-align: top; }}
  .line-num {{
    width: 50px; text-align: right; color: #484f58; user-select: none;
    padding: 0 12px !important; background: #0d1117; font-size: 12px;
  }}
  .line-add {{ background: #0f2d1a; }}
  .line-add .line-num {{ background: #0f2d1a; color: #3fb950; }}
  .line-add .line-code::before {{ content: "+"; color: #3fb950; margin-right: 8px; font-weight: bold; }}
  .line-del {{ background: #2d1215; }}
  .line-del .line-num {{ background: #2d1215; color: #f85149; }}
  .line-del .line-code::before {{ content: "-"; color: #f85149; margin-right: 8px; font-weight: bold; }}
  .line-ctx .line-code::before {{ content: " "; margin-right: 8px; }}
  .line-code {{ width: 100%; color: #c9d1d9; }}
  .hunk-line {{ background: #0d1117; color: #8b949e; font-style: italic; padding: 2px 12px !important; }}
  .file-count {{ color: #8b949e; font-size: 13px; text-align: center; padding: 16px; }}
</style>
</head>
<body>
<div class="header">
  <h1>{title} <span class="add-badge">+{total_add}</span> <span class="del-badge">-{total_del}</span></h1>
  <div class="meta">
    <span>生成: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</span>
    <span>共 {len(files)} 个文件</span>
  </div>
</div>
'''
    
    for idx, f in enumerate(files):
        f_add = sum(1 for l in f['lines'] if l['type'] == 'add')
        f_del = sum(1 for l in f['lines'] if l['type'] == 'del')
        html += f'''<div class="file-section">
  <div class="file-header" onclick="toggleFile({idx})">
    <span><span class="arrow">▶</span>{f['name']}</span>
    <span class="stats"><span class="add">+{f_add}</span> <span class="del">-{f_del}</span></span>
  </div>
  <div class="file-content" id="file-{idx}">
<table class="diff-table">
'''
        for l in f['lines']:
            if l['type'] == 'hunk':
                code = l['code'].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
                html += f'  <tr><td class="hunk-line" colspan="3">{code}</td></tr>\n'
                continue
            old_n = l['old_num']
            new_n = l['new_num']
            code = l['code'].replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            html += f'  <tr class="line-{l["type"]}">\n    <td class="line-num">{old_n}</td>\n    <td class="line-num">{new_n}</td>\n    <td class="line-code">{code}</td>\n  </tr>\n'
        html += '</table>\n  </div>\n</div>\n'
    
    html += '''<script>
function toggleFile(idx) {
  const el = document.getElementById('file-' + idx);
  const arrow = event.currentTarget.querySelector('.arrow');
  el.classList.toggle('open');
  arrow.textContent = el.classList.contains('open') ? '▼' : '▶';
}
// 默认展开第一个文件
if (document.querySelector('.file-content')) {
  document.querySelector('.file-content').classList.add('open');
  document.querySelector('.arrow').textContent = '▼';
}
</script>
</body>
</html>'''
    return html

if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else '.'
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(path, 'diff.html')
    
    diff_text = run_git_diff(path)
    if not diff_text.strip():
        print("没有差异")
        sys.exit(0)
    
    files = parse_diff(diff_text)
    html = generate_html(files)
    
    with open(out, 'w', encoding='utf-8') as f:
        f.write(html)
    
    print(f"Diff HTML: {out}")
    print(f"共 {len(files)} 个文件, +{sum(1 for f in files for l in f['lines'] if l['type']=='add')} -{sum(1 for f in files for l in f['lines'] if l['type']=='del')}")
