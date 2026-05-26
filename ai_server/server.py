from flask import Flask, request, jsonify
import re
import math

app = Flask(__name__)

# Simula la logica di un modello linguistico/spaziale
# Trasforma il prompt in linguaggio naturale in coordinate spaziali per FAIRWORLD
def generate_blocks(prompt, player_x, player_y, player_z):
    blocks = []
    px, py, pz = int(player_x), int(player_y), int(player_z)
    
    prompt = prompt.lower()
    
    # 1. Monolite Alieno
    if "monolite" in prompt or "alieno" in prompt:
        for y in range(py, py + 10):
            for x in range(px - 1, px + 2):
                for z in range(pz - 1, pz + 2):
                    blocks.append({"x": x, "y": y, "z": z, "id": 3}) # Pietra
                    
    # 2. Ponte ad arco
    elif "ponte" in prompt:
        length = 10
        for i in range(length):
            # Parabole per l'arco
            height_offset = int(math.sin(i / length * math.pi) * 3)
            blocks.append({"x": px, "y": py + height_offset, "z": pz + i, "id": 4}) # Legno
            blocks.append({"x": px - 1, "y": py + height_offset, "z": pz + i, "id": 4})
            blocks.append({"x": px + 1, "y": py + height_offset, "z": pz + i, "id": 4})

    # 3. Muro (fallback intelligente)
    elif "muro" in prompt:
        for x in range(px - 3, px + 4):
            for y in range(py, py + 3):
                blocks.append({"x": x, "y": y, "z": pz, "id": 3})
                
    # Fallback: se non capisce, genera una piramide
    else:
        radius = 3
        for y in range(py, py + radius):
            layer_radius = radius - (y - py)
            for x in range(px - layer_radius, px + layer_radius + 1):
                for z in range(pz - layer_radius, pz + layer_radius + 1):
                    blocks.append({"x": x, "y": y, "z": z, "id": 3})

    return blocks

@app.route('/generate', methods=['POST'])
def generate():
    data = request.json
    if not data:
        return jsonify({"error": "No JSON payload"}), 400
        
    prompt = data.get("prompt", "")
    px = data.get("x", 0)
    py = data.get("y", 0)
    pz = data.get("z", 0)
    
    print(f"[AI SERVER] Ricevuta richiesta: '{prompt}' alle coordinate {px}, {py}, {pz}")
    
    blocks = generate_blocks(prompt, px, py, pz)
    
    print(f"[AI SERVER] Restituiti {len(blocks)} blocchi.")
    return jsonify(blocks)

if __name__ == '__main__':
    # Esegue il server su localhost:5000
    app.run(host='127.0.0.1', port=5000, debug=True)
