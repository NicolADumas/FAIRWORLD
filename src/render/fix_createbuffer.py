import re

rm_cpp = "D:/FAIRWORLD/FAIRWORLD/src/render/RenderManager.cpp"

with open(rm_cpp, 'r') as f:
    content = f.read()

# Trova tutti i richiami a CreateBuffer(...) e sostituiscili con m_memory->CreateBuffer(...)
# Facciamo attenzione a non sostituire cose sbagliate.
content = re.sub(r'(?<!::)\bCreateBuffer\(', 'm_memory->CreateBuffer(', content)

with open(rm_cpp, 'w') as f:
    f.write(content)
