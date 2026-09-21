"""Run inside Unreal Editor Python; creates a review scene and reusable mesh kit.

Run once into an empty destination. No runtime scripts or Blueprints are produced.
"""
import unreal as u
import math

ROOT = '/Game/Phase2/HanRiver'
MAP = ROOT + '/Maps/L_HanRiver_BlueHour'
if u.EditorAssetLibrary.does_asset_exist(MAP):
    raise RuntimeError('Refusing to overwrite existing Han review level')
world = u.EditorLoadingAndSavingUtils.new_blank_map(False)
actors = u.get_editor_subsystem(u.EditorActorSubsystem)
assets = u.AssetToolsHelpers.get_asset_tools()
meshes = u.get_editor_subsystem(u.StaticMeshEditorSubsystem)
materials = {}

def material(name, color, roughness=0.7, emissive=False):
    path = ROOT + '/Materials/M_Han_' + name
    m = u.load_asset(path)
    if m is not None:
        materials[name] = m
        return m
    m = assets.create_asset('M_Han_' + name, ROOT + '/Materials', u.Material, u.MaterialFactoryNew())
    c = u.MaterialEditingLibrary.create_material_expression(m, u.MaterialExpressionConstant3Vector, -400, 0)
    c.set_editor_property('constant', u.LinearColor(*color, 1))
    u.MaterialEditingLibrary.connect_material_property(c, '', u.MaterialProperty.MP_BASE_COLOR)
    r = u.MaterialEditingLibrary.create_material_expression(m, u.MaterialExpressionConstant, -400, 160)
    r.set_editor_property('r', roughness)
    u.MaterialEditingLibrary.connect_material_property(r, '', u.MaterialProperty.MP_ROUGHNESS)
    if emissive:
        u.MaterialEditingLibrary.connect_material_property(c, '', u.MaterialProperty.MP_EMISSIVE_COLOR)
    u.MaterialEditingLibrary.recompile_material(m)
    u.EditorAssetLibrary.set_metadata_tag(m, 'VIR.Source', 'Original VIR editor-authored material; review candidate')
    u.EditorAssetLibrary.save_loaded_asset(m)
    materials[name] = m
    return m

for name, color, rough, glow in [
    ('Concrete', (.25,.29,.34), .72, False),
    ('Steel', (.065,.10,.15), .42, False),
    ('WarmLight', (2.0,.95,.32), .45, True),
    ('Glass', (.055,.15,.20), .24, False),
    ('Canopy', (.055,.14,.095), .9, False),
    ('Bark', (.10,.065,.04), .9, False),
    ('Bank', (.13,.18,.15), .95, False),
    ('Water', (.018,.065,.105), .3, False),
    ('Skyline', (.13,.18,.25), .75, False)]:
    material(name, color, rough, glow)

def part(name, shape, loc, scale, mat, folder, rot=(0,0,0)):
    a = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(*loc), u.Rotator(*rot))
    a.set_actor_label(name)
    a.set_folder_path(folder)
    c = a.static_mesh_component
    c.set_static_mesh(u.load_asset('/Engine/BasicShapes/' + shape))
    c.set_material(0, materials[mat])
    c.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(u.Vector(*scale))
    return a

def kit(name, parts):
    opts = u.MergeStaticMeshActorsOptions()
    opts.base_package_name = ROOT + '/Meshes/SM_Han_' + name
    opts.destroy_source_actors = False
    opts.spawn_merged_actor = False
    opts.new_actor_label = 'Han_' + name
    merged = meshes.merge_static_mesh_actors(parts, opts)
    # The merge tool prefixes the asset name with "SM_", so the created asset is
    # SM_SM_Han_<name>; try both spellings so the post-merge steps actually run.
    leaf = 'SM_Han_' + name
    mesh = u.load_asset(opts.base_package_name) or u.load_asset(ROOT + '/Meshes/SM_' + leaf)
    if mesh:
        meshes.remove_collisions(mesh)
        u.EditorAssetLibrary.set_metadata_tag(mesh, 'VIR.Source', 'Original VIR assembly of Unreal Engine basic primitives; review candidate')
        u.EditorAssetLibrary.save_loaded_asset(mesh)
    else:
        # UE 5.8 can decline a merge when source components are transient or
        # when the editor is still compiling a material. Keep the authored
        # component assembly in the review map rather than losing the scene.
        print('HAN_AUTHORING_MERGE_SKIPPED: ' + opts.base_package_name)

# 500 m representative scene. X follows the row, Y crosses the river; cm units.
part('Han_Water_500m', 'Cube', (20000,0,-30), (650,480,.4), 'Water', 'Han/River')
for side in [-1,1]:
    part('ParkBank', 'Cube', (20000,side*25500,90), (650,90,2), 'Bank', 'Han/Banks')
    for step in range(4):
        part('SteppedQuay', 'Cube', (20000,side*(21300+step*350),step*55), (650,7,1), 'Concrete', 'Han/Banks')

# Broad bridge with deep girders, paired piers, cornice, rail and lamp detail.
bridge=[]
def bp(name, shape, loc, scale, mat):
    bridge.append(part(name,shape,loc,scale,mat,'Han/BanpoBridge'))
bp('Deck', 'Cube',(6500,0,1550),(14,470,1.4),'Concrete')
for x in [5950,6450,6950]:
    bp('LongitudinalGirder','Cube',(x,0,1410),(1.2,470,2),'Steel')
for y in [-19000,-12500,12500,19000]:
    bp('PierFoot','Cube',(6500,y,50),(18,12,2),'Concrete')
    for x in [6120,6880]:
        bp('PierColumn','Cylinder',(x,y,740),(3.6,5.5,14),'Concrete')
    bp('PierCap','Cube',(6500,y,1360),(18,8,2),'Concrete')
for x in [5800,7200]:
    bp('Cornice','Cube',(x,0,1630),(.6,470,.6),'Concrete')
    bp('Rail','Cube',(x,0,1770),(.16,470,.18),'Steel')
    for y in range(-22500,22501,1500):
        bp('RailPost','Cube',(x,y,1710),(.16,.16,1.5),'Steel')
    for y in range(-21000,21001,3500):
        bp('LampStem','Cylinder',(x,y,1880),(.12,.12,5),'Steel')
        bp('LampGlow','Sphere',(x,y,2135),(.45,.45,.25),'WarmLight')
kit('BridgeSpan',bridge)

# Three contemporary waterside pavilions; original massing, no exact replicas.
for k in range(3):
    x=14000+k*3800
    group=[]
    def pp(n,shape,loc,scale,mat):
        group.append(part(n,shape,loc,scale,mat,'Han/SevitPavilions'))
    pp('IslandPlinth','Cylinder',(x,-18300,85),(30,22,1.5),'Concrete')
    pp('PavilionGlass','Cylinder',(x,-18300,620),(23,17,10),'Glass')
    pp('FloatingRoof','Cylinder',(x,-18300,1160),(27,20,.8),'Steel')
    pp('WarmCornice','Cylinder',(x,-18300,1080),(23.3,17.3,.18),'WarmLight')
    for j in range(16):
        t=j*math.tau/16
        pp('FacadeFin','Cube',(x+1150*math.cos(t),-18300+850*math.sin(t),620),(.25,.25,10),'Steel')
    if k==0:
        kit('RiverPavilion',group)

tree=[]
for i in range(44):
    side=-1 if i%2 else 1
    x=-6000+(i//2)*2600
    y=side*(23900+(i%3)*500)
    trunk=part('ParkTreeTrunk','Cylinder',(x,y,350),(.65,.65,7),'Bark','Han/Planting')
    crown=part('ParkTreeCanopy','Sphere',(x,y,970),(6.5,5.5,8),'Canopy','Han/Planting')
    crown2=part('ParkTreeCanopy','Sphere',(x+220,y+100,820),(5,5,6),'Canopy','Han/Planting')
    if i==0:
        tree=[trunk,crown,crown2]
kit('ParkTree',tree)

for side in [-1,1]:
    for i in range(30):
        x=-7000+i*2200
        height=1800+(i*791%4500)
        y=side*(31000+(i%3)*1800)
        part('CityBlock','Cube',(x,y,height/2),(12+(i%4)*3,16,height/100),'Skyline','Han/Skyline')
        for floor in range(2,int(height/450)):
            part('WindowBand','Cube',(x,y-side*805,floor*450),(8,.04,.35),'WarmLight','Han/Skyline')
    for i in range(24):
        x=-6000+i*2600
        part('PromenadeLightPost','Cylinder',(x,side*22900,230),(.12,.12,4.6),'Steel','Han/Promenade')
        part('PromenadeLightHead','Sphere',(x,side*22900,470),(.5,.5,.22),'WarmLight','Han/Promenade')

sun=actors.spawn_actor_from_class(u.DirectionalLight,u.Vector(0,0,12000),u.Rotator(-25,-30,0))
sun.set_actor_label('Han_BlueHour_Key')
sun.light_component.set_intensity(2.0)
sun.light_component.set_light_color(u.LinearColor(.48,.62,1.0))
sky=actors.spawn_actor_from_class(u.SkyLight,u.Vector(0,0,10000))
sky.light_component.set_intensity(1.0)
atmosphere=actors.spawn_actor_from_class(u.SkyAtmosphere,u.Vector(0,0,0))
fog=actors.spawn_actor_from_class(u.ExponentialHeightFog,u.Vector(0,0,0))
fog.component.set_editor_property('fog_density',0.008)
for a in [sun,sky,atmosphere,fog]:
    a.set_folder_path('Han/Lighting')
u.EditorLevelLibrary.set_level_viewport_camera_info(u.Vector(-8000,-6000,1800),u.Rotator(-3,14,0))
assert u.EditorLoadingAndSavingUtils.save_map(world,MAP)
u.EditorAssetLibrary.save_directory(ROOT,only_if_is_dirty=True,recursive=True)
print('HAN_AUTHORING_COMPLETE: '+MAP+'; actors='+str(len(actors.get_all_level_actors())))
