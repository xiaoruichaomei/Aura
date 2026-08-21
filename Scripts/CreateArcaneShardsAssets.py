import unreal

ASSET_ROOT = '/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/ArcaneShards'

def ensure_directory(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)

def create_blueprint(name, parent, path):
    asset_path = path + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property('parent_class', parent)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, unreal.Blueprint, factory)
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return asset

def duplicate_asset(name, source):
    asset_path = ASSET_ROOT + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        return unreal.EditorAssetLibrary.load_asset(asset_path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().duplicate_asset(name, ASSET_ROOT, unreal.load_asset(source))
    unreal.EditorAssetLibrary.save_asset(asset_path, only_if_is_dirty=False)
    return asset

def set_property_if_present(obj, name, value):
    try:
        obj.set_editor_property(name, value)
        return True
    except Exception as error:
        unreal.log_warning('Skipped {}: {}'.format(name, error))
        return False

ensure_directory(ASSET_ROOT)
ensure_directory('/Game/Blueprints/Actor/MagicCircle')
ensure_directory('/Game/Blueprints/AbilitySystem/Cues')

arcane_class = unreal.load_class(None, '/Script/Aura.AuraArcaneShards')
circle_class = unreal.load_class(None, '/Script/Aura.MagicCircle')
ga = create_blueprint('GA_ArcaneShards', arcane_class, ASSET_ROOT)
circle = create_blueprint('BP_MagicCircle', circle_class, '/Game/Blueprints/Actor/MagicCircle')
cost = duplicate_asset('GE_Cost_ArcaneShards', '/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/FireBolt/Aura/GE_Cost_FireBolt')
cooldown = duplicate_asset('GE_Cooldown_ArcaneShards', '/Game/Blueprints/AbilitySystem/GameplayAbilities/Attack/Ranged/FireBolt/Aura/GE_Cooldown_FireBolt')
cue = duplicate_asset('GC_ArcaneShards', '/Game/Blueprints/AbilitySystem/Cues/GC_MeleeImpact')
montage = duplicate_asset('AM_Cast_ArcaneShards', '/Game/Assets/Characters/Aura/Animations/Abilities/AM_Cast_FireBolt')

ga_cdo = unreal.get_default_object(ga.generated_class())
set_property_if_present(ga_cdo, 'cost_gameplay_effect_class', cost)
set_property_if_present(ga_cdo, 'cooldown_gameplay_effect_class', cooldown)
set_property_if_present(ga_cdo, 'magic_circle_material', unreal.load_asset('/Game/Assets/MagicCircles/M_MagicCircle_1'))
unreal.EditorAssetLibrary.save_asset(ASSET_ROOT + '/GA_ArcaneShards', only_if_is_dirty=False)

unreal.log('Arcane Shards assets created. Configure GE modifiers, cue Niagara/Sound, montage, and AbilityInfo in the editor.')
