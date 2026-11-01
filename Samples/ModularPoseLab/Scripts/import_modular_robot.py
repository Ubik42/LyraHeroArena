import unreal,json
from pathlib import Path
root=Path(unreal.Paths.project_dir());folder='/Game/ModularPoseLab';a=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools();records=[];skeleton=None
try:
    unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.FBX 0')
    for part in ['Full','Lower','Upper','Head']:
        name='SK_Robot'+part
        if a.does_asset_exist(folder+'/'+name) and '-ModularPoseReimport' not in unreal.SystemLibrary.get_command_line():raise RuntimeError('Preserve existing '+name)
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(root/'ContentSource/ModularPoseLab'/('Robot'+part+'.fbx')),destination_path=folder,destination_name=name,automated=True,replace_existing=True,save=True).items():task.set_editor_property(k,v)
        options=unreal.FbxImportUI()
        for k,v in dict(import_as_skeletal=True,import_mesh=True,import_animations=part=='Full',import_materials=True,import_textures=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH).items():options.set_editor_property(k,v)
        if skeleton:options.set_editor_property('skeleton',skeleton)
        task.set_editor_property('options',options);tools.import_asset_tasks([task])
        mesh=a.load_asset(folder+'/'+name)
        if not isinstance(mesh,unreal.SkeletalMesh):raise RuntimeError('Missing skeletal '+name)
        if part=='Full':skeleton=mesh.get_editor_property('skeleton')
        records.append({'part':part,'mesh':mesh.get_path_name(),'skeleton':mesh.get_editor_property('skeleton').get_path_name(),'imported':list(task.get_editor_property('imported_object_paths'))})
    a.save_directory(folder,only_if_is_dirty=True,recursive=True)
    assets=[]
    for path in a.list_assets(folder,recursive=True,include_folder=False):
        obj=a.load_asset(path);assets.append({'path':path,'class':obj.get_class().get_name()})
    out=root/'Saved/ModularPoseLab';out.mkdir(exist_ok=True)
    (out/'import.json').write_text(json.dumps({'parts':records,'assets':assets},indent=2),encoding='utf-8')
    unreal.log('MODULAR_ROBOT_IMPORT_OK')
finally:unreal.SystemLibrary.execute_console_command(None,'QUIT_EDITOR')
