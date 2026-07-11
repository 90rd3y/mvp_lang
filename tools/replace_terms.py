import os

replacements = {
    "семантическая ошибка": "статическая ошибка",
    "синтаксическая ошибка": "статическая ошибка",
    "компилятор": "интерпретатор",
    "Компилятор": "Интерпретатор",
    "ошибка компиляции": "статическая ошибка",
    "ошибки компиляции": "статические ошибки",
    "на этапе компиляции": "на этапе статического анализа",
    "компилируется": "интерпретируется",
    "компиляции": "статического анализа"
}

def process_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    new_content = content
    for old, new in replacements.items():
        new_content = new_content.replace(old, new)
    
    if new_content != content:
        with open(filepath, 'w') as f:
            f.write(new_content)
        print(f"Updated {filepath}")

for root, _, files in os.walk('.'):
    if '.git' in root or 'tools' in root or 'archive' in root:
        continue
    for file in files:
        if file.endswith(('.cpp', '.hpp', '.md')):
            process_file(os.path.join(root, file))
