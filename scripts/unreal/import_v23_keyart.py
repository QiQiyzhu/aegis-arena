"""Import the original v2.3 briefing illustration without touching existing art."""
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
source = root / 'Art/v23/prism-archive-keyart.png'
destination = '/Game/Aegis/V23/UI/T_PrismArchive'
if not source.is_file():
    raise RuntimeError('Missing project-owned source illustration')
if unreal.EditorAssetLibrary.does_asset_exist(destination):
    raise RuntimeError('Refusing to overwrite an existing authored texture')
task = unreal.AssetImportTask()
task.set_editor_property('filename', str(source))
task.set_editor_property('destination_path', '/Game/Aegis/V23/UI')
task.set_editor_property('destination_name', 'T_PrismArchive')
task.set_editor_property('automated', True)
task.set_editor_property('save', True)
task.set_editor_property('replace_existing', False)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
texture = unreal.load_asset(destination)
if not isinstance(texture, unreal.Texture2D):
    raise RuntimeError('Texture import did not create the expected asset')
texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_DEFAULT)
texture.set_editor_property('never_stream', True)
if not unreal.EditorAssetLibrary.save_loaded_asset(texture):
    raise RuntimeError('Texture save failed')
unreal.log('AEGIS_V23_KEYART_IMPORTED ' + destination)
