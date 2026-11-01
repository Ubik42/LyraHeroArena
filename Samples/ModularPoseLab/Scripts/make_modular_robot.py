"""Original rigid-skinned robot, shared full hierarchy, authored walking cycle."""
import bpy,math
from pathlib import Path
from mathutils import Vector
out=Path(__file__).resolve().parents[1]/'ContentSource/ModularPoseLab';out.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
colors={'blue':(.045,.25,.35,1),'orange':(.9,.25,.035,1),'joint':(.035,.045,.06,1),'visor':(.03,.8,.7,1)}
mats={}
for name,c in colors.items():
 m=bpy.data.materials.new(name);m.diffuse_color=c;mats[name]=m
bpy.ops.object.armature_add();rig=bpy.context.object;rig.name='RobotRig';bpy.ops.object.mode_set(mode='EDIT');arm=rig.data
arm.edit_bones.remove(arm.edit_bones[0])
bones=[('root',(0,0,0),(0,0,.2),None),('pelvis',(0,0,.9),(0,0,1.05),'root'),('spine',(0,0,1.05),(0,0,1.43),'pelvis'),('neck',(0,0,1.43),(0,0,1.56),'spine'),('head',(0,0,1.56),(0,0,1.85),'neck')]
for side,sign in [('l',1),('r',-1)]:
 bones += [(f'thigh_{side}',(sign*.17,0,.95),(sign*.17,0,.54),'pelvis'),(f'calf_{side}',(sign*.17,0,.54),(sign*.17,0,.14),f'thigh_{side}'),(f'foot_{side}',(sign*.17,0,.14),(sign*.17,-.25,.09),f'calf_{side}'),(f'upperarm_{side}',(sign*.32,0,1.4),(sign*.58,0,1.15),'spine'),(f'forearm_{side}',(sign*.58,0,1.15),(sign*.72,0,.91),f'upperarm_{side}')]
for name,h,t,parent in bones:
 b=arm.edit_bones.new(name);b.head=h;b.tail=t
 if parent:b.parent=arm.edit_bones[parent]
bpy.ops.object.mode_set(mode='OBJECT')
parts={'Lower':[],'Upper':[],'Head':[]}
def block(part,name,loc,scale,bone,color):
 bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.scale=scale
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 b=o.modifiers.new('soft-edge','BEVEL');b.width=.025;b.segments=3;bpy.ops.object.modifier_apply(modifier=b.name)
 o.data.materials.append(mats[color]);vg=o.vertex_groups.new(name=bone);vg.add(list(range(len(o.data.vertices))),1,'REPLACE')
 parts[part].append(o)
def limb(part,name,h,t,width,bone,color):
 mid=(Vector(h)+Vector(t))*.5;length=(Vector(t)-Vector(h)).length
 block(part,name,mid,(width,width,length),bone,color);o=parts[part][-1];o.rotation_mode='QUATERNION';o.rotation_quaternion=(Vector(t)-Vector(h)).to_track_quat('Z','Y')
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=False)
block('Lower','hips',(0,0,1.0),(.46,.25,.23),'pelvis','joint')
block('Upper','chest',(0,0,1.27),(.55,.3,.4),'spine','blue')
block('Upper','chest-panel',(0,-.162,1.3),(.26,.03,.13),'spine','orange')
block('Head','helmet',(0,0,1.68),(.36,.32,.34),'head','orange')
block('Head','visor',(0,-.175,1.71),(.31,.035,.12),'head','visor')
for side,sign in [('l',1),('r',-1)]:
 for name,h,t,parent in bones:
  if not name.endswith('_'+side):continue
  if name.startswith('thigh'):limb('Lower',name,h,t,.19,name,'blue')
  elif name.startswith('calf'):limb('Lower',name,h,t,.16,name,'orange')
  elif name.startswith('foot'):block('Lower',name,(sign*.17,-.085,.08),(.2,.36,.14),name,'joint')
  else:limb('Upper',name,h,t,.16,name,'blue' if name.startswith('upper') else 'orange')
joined={}
for part,objects in parts.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0];bpy.ops.object.join();o=bpy.context.object;o.name='SK_Robot'+part
 bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
 mod=o.modifiers.new('Rig','ARMATURE');mod.object=rig;o.parent=rig;joined[part]=o
# The complete hidden leader includes weighted geometry for every animated limb.
bpy.ops.object.select_all(action='DESELECT');copies=[]
for o in joined.values():
 c=o.copy();c.data=o.data.copy();bpy.context.collection.objects.link(c);c.select_set(True);copies.append(c)
bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();full=bpy.context.object;full.name='SK_RobotFull'
scene=bpy.context.scene;scene.render.fps=30;scene.frame_start=1;scene.frame_end=61
for f in range(1,62,5):
 phase=(f-1)/60*math.tau
 for side,sign in [('l',1),('r',-1)]:
  for prefix,angle in [('thigh',.48*math.sin(phase)*sign),('calf',.45*max(0,-math.sin(phase)*sign)),('foot',-.12*math.sin(phase)*sign),('upperarm',-.35*math.sin(phase)*sign),('forearm',.16+.12*math.sin(phase)*sign)]:
   p=rig.pose.bones[prefix+'_'+side];p.rotation_mode='XYZ';p.rotation_euler=(angle,0,0);p.keyframe_insert(data_path='rotation_euler',frame=f)
 p=rig.pose.bones['pelvis'];p.location=(0,0,.018*math.cos(phase*2));p.keyframe_insert(data_path='location',frame=f)
rig.animation_data.action.name='RobotWalk'
for name,o in [('Full',full),*joined.items()]:
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);rig.select_set(True);bpy.context.view_layer.objects.active=rig
 bpy.ops.export_scene.fbx(filepath=str(out/('Robot'+name+'.fbx')),use_selection=True,object_types={'ARMATURE','MESH'},add_leaf_bones=False,bake_anim=name=='Full',bake_anim_use_all_actions=False,bake_anim_use_nla_strips=False,bake_anim_simplify_factor=0,armature_nodetype='NULL')
bpy.ops.wm.save_as_mainfile(filepath=str(out/'modular-robot.blend'))
