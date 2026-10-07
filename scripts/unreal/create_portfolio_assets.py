"""Create only new original Portfolio materials and synthesized sound assets."""
import array
import math
from pathlib import Path
import random
import wave
import unreal

assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
for name, metallic, rough, emission in [('M_PortfolioMetal', 0.45, 0.38, 0.02), ('M_PortfolioGlow', 0.0, 0.45, 2.4)]:
    path = '/Game/Aegis/Materials/' + name
    if assets.does_asset_exist(path):
        material = unreal.load_asset(path)
        material.set_editor_property('used_with_instanced_static_meshes', True)
        edit.recompile_material(material)
        assert assets.save_loaded_asset(material)
        unreal.log('Portfolio instancing usage verified: ' + path)
        continue
    material = tools.create_asset(name, '/Game/Aegis/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('used_with_instanced_static_meshes', True)
    tint = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -500, 0)
    tint.set_editor_property('parameter_name', 'Tint')
    tint.set_editor_property('default_value', unreal.LinearColor(0.3, 0.4, 0.5, 1))
    edit.connect_material_property(tint, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop, y in [(metallic, unreal.MaterialProperty.MP_METALLIC, 150), (rough, unreal.MaterialProperty.MP_ROUGHNESS, 250)]:
        constant = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -500, y)
        constant.set_editor_property('r', value)
        edit.connect_material_property(constant, '', prop)
    power = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 350)
    power.set_editor_property('r', emission)
    multiply = edit.create_material_expression(material, unreal.MaterialExpressionMultiply, -200, 100)
    edit.connect_material_expressions(tint, '', multiply, 'A')
    edit.connect_material_expressions(power, '', multiply, 'B')
    edit.connect_material_property(multiply, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(material)
    assert assets.save_loaded_asset(material)

root = Path(unreal.Paths.project_dir()).resolve()
audio_dir = root / 'assets/source_audio'
audio_dir.mkdir(parents=True, exist_ok=True)
specs = [('S_Shot', .13), ('S_Impact', .12), ('S_Pulse', .65), ('S_Break', .48), ('S_Upgrade', .65), ('S_Repair', .55)]
tasks = []
for name, duration in specs:
    rate = 24000
    rng = random.Random(name)
    samples = array.array('h')
    for i in range(int(rate * duration)):
        t = i / rate
        u = t / duration
        noise = rng.uniform(-1, 1)
        attack = min(1, t / .004)
        if name == 'S_Shot':
            value = .65 * math.sin(2 * math.pi * (190 * t - 420 * t*t)) + .3 * noise
            envelope = math.exp(-u * 8)
        elif name == 'S_Impact':
            value = .5 * noise + .4 * math.sin(2 * math.pi * 840 * t)
            envelope = math.exp(-u * 9)
        elif name == 'S_Pulse':
            value = .55 * math.sin(2 * math.pi * (100*t - 40*t*t)) + .15 * noise + .15 * math.sin(2*math.pi*(900*t-550*t*t))
            envelope = math.exp(-u * 5)
        elif name == 'S_Break':
            value = .55 * noise + .2 * math.sin(2*math.pi*68*t)
            envelope = math.exp(-u * 6)
        else:
            notes = [440, 554.37, 659.25] if name == 'S_Upgrade' else [523.25, 659.25, 783.99]
            value = sum(math.sin(2*math.pi*f*t) for f in notes) / 3
            envelope = math.sin(math.pi*u)**1.5 * .55
        samples.append(int(max(-1,min(1,value * envelope * attack)) * 22000))
    path = audio_dir / (name + '.wav')
    with wave.open(str(path), 'wb') as f:
        f.setnchannels(1); f.setsampwidth(2); f.setframerate(rate); f.writeframes(samples.tobytes())
    if not assets.does_asset_exist('/Game/Aegis/Audio/' + name):
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(path))
        task.set_editor_property('destination_path', '/Game/Aegis/Audio')
        task.set_editor_property('automated', True)
        task.set_editor_property('save', True)
        tasks.append(task)
tools.import_asset_tasks(tasks)
for name, _ in specs:
    assert assets.does_asset_exist('/Game/Aegis/Audio/' + name)
unreal.log('AEGIS_PORTFOLIO_ASSETS_COMPLETE materials=2 sounds=6 original_synthesis=1')
