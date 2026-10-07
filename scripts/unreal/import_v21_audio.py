"""Import only new v2.1 audio, after running generate_v21_audio.py externally.

Use UnrealEditor-Cmd project -run=pythonscript -script=this_file. No old audio is
changed. Every imported WAV is hash-bound to the original synthesis manifest.
"""
import hashlib
import json
from pathlib import Path
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
manifest=json.loads((root/'assets/v21_source_audio/manifest.json').read_text(encoding='utf-8'))
base='/Game/Aegis/V21/Audio'
tasks=[]
for entry in manifest['entries']:
    source=root/entry['source']
    assert hashlib.sha256(source.read_bytes()).hexdigest()==entry['sha256'],source
    task=unreal.AssetImportTask()
    task.set_editor_property('filename',str(source))
    task.set_editor_property('destination_path',base)
    task.set_editor_property('destination_name',entry['asset'])
    task.set_editor_property('automated',True)
    task.set_editor_property('replace_existing',True)
    task.set_editor_property('save',True)
    tasks.append(task)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
checked=[]
for entry in manifest['entries']:
    sound=unreal.load_asset(base+'/'+entry['asset'])
    assert isinstance(sound,unreal.SoundWave),entry['asset']
    sound.set_editor_property('looping',entry['loop'])
    sound.set_editor_property('volume',1.0)
    sound.set_editor_property('pitch',1.0)
    sound.set_editor_property('compression_quality',85 if entry['asset'].startswith('M_') else 90)
    # Music starts silent during its game-clock fade. Virtualize at zero so the
    # audio renderer does not discard it before it becomes audible.
    sound.set_editor_property('virtualization_mode',unreal.VirtualizationMode.PLAY_WHEN_SILENT)
    sound.set_editor_property('loading_behavior',unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
    assert bool(sound.get_editor_property('looping'))==entry['loop']
    assert sound.get_editor_property('virtualization_mode')==unreal.VirtualizationMode.PLAY_WHEN_SILENT
    assert unreal.EditorAssetLibrary.save_loaded_asset(sound),entry['asset']
    checked.append({'asset':entry['asset'],'loop':bool(sound.get_editor_property('looping')),
                    'virtualization':'PLAY_WHEN_SILENT','sourceSha256':entry['sha256']})
report={'passed':len(checked)==24,'assets':checked,
        'sourceManifestSha256':hashlib.sha256((root/'assets/v21_source_audio/manifest.json').read_bytes()).hexdigest()}
output=root/'Saved/AegisV21/audio-import.json'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
unreal.log('AEGIS_V21_AUDIO_COMPLETE assets=24 music=7 sfx=17 loops=5 virtualize=play_when_silent')
