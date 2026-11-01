import unreal,json
from pathlib import Path
r=Path(unreal.Paths.project_dir());F='/Game/ModularPoseLab';a=unreal.EditorAssetLibrary;ml=unreal.MaterialEditingLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
try:
    mats={}
    for name,color in {'blue':(.045,.25,.35),'orange':(.9,.25,.035),'joint':(.035,.045,.06),'visor':(.03,.8,.7)}.items():
        m=a.load_asset(F+'/M_'+name) if a.does_asset_exist(F+'/M_'+name) else tools.create_asset('M_'+name,F,unreal.Material,unreal.MaterialFactoryNew())
        ml.delete_all_material_expressions(m)
        c=ml.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);c.set_editor_property('constant',unreal.LinearColor(*color,1));ml.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
        rough=ml.create_material_expression(m,unreal.MaterialExpressionConstant);rough.set_editor_property('r',.35);ml.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
        ml.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH);ml.recompile_material(m);a.save_loaded_asset(m);mats[name]=m
    slots=[]
    for part in ['Full','Lower','Upper','Head']:
        mesh=a.load_asset(F+'/SK_Robot'+part);mesh.set_editor_property('physics_asset',None)
        materials=list(mesh.get_editor_property('materials'))
        for i,slot in enumerate(materials):
            name=str(slot.get_editor_property('material_slot_name'));slots.append([part,i,name])
            slot.set_editor_property('material_interface',mats.get(name,mats['blue']))
        mesh.set_editor_property('materials',materials)
        a.save_loaded_asset(mesh)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(r/'ContentSource/ModularPoseLab/RobotFull.fbx'),destination_path=F,destination_name='SK_RobotFull_Anim',automated=True,replace_existing=False,save=True).items():task.set_editor_property(k,v)
    o=unreal.FbxImportUI()
    for k,v in dict(import_as_skeletal=True,import_mesh=False,import_animations=True,import_materials=False,import_textures=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_ANIMATION,skeleton=a.load_asset(F+'/SK_RobotFull_Skeleton')).items():o.set_editor_property(k,v)
    task.set_editor_property('options',o)
    if not a.does_asset_exist(F+'/SK_RobotFull_Anim'):tools.import_asset_tasks([task])
    a.save_directory(F,only_if_is_dirty=True,recursive=True)
    anims=[x for x in a.list_assets(F,recursive=True,include_folder=False) if isinstance(a.load_asset(x),unreal.AnimSequence)]
    if len(anims)!=1:raise RuntimeError('Expected one animation: '+str(anims))
    if anims[0].split('.')[-1]!='SK_RobotFull_Anim':assert a.rename_asset(anims[0],F+'/SK_RobotFull_Anim')
    a.save_directory(F,only_if_is_dirty=False,recursive=True)
    (r/'Saved/ModularPoseLab/dependencies.json').write_text(json.dumps({'slots':slots,'animations':anims},indent=2))
    unreal.log('MODULAR_DEPENDENCIES_OK')
finally:unreal.SystemLibrary.execute_console_command(None,'QUIT_EDITOR')
