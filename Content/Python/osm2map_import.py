import unreal


def _set_property(obj, names, value):
    last_error = None
    for name in names:
        try:
            obj.set_editor_property(name, value)
            return
        except Exception as exc:
            last_error = exc
    raise last_error


def _make_settings(
    osm_file_path,
    elevation_data_path="",
    world_scale=100.0,
    import_terrain=True,
    import_roads=True,
    import_buildings=True,
    import_assets=True,
    landscape_resolution=1009,
    default_building_height=9.0,
    road_z_offset=2.0,
    asset_dictionary=None,
):
    settings = unreal.OsmImportSettings()
    _set_property(settings, ["osm_file_path"], osm_file_path)
    _set_property(settings, ["elevation_data_path"], elevation_data_path)
    _set_property(settings, ["world_scale"], world_scale)
    _set_property(settings, ["import_terrain", "b_import_terrain"], import_terrain)
    _set_property(settings, ["import_roads", "b_import_roads"], import_roads)
    _set_property(settings, ["import_buildings", "b_import_buildings"], import_buildings)
    _set_property(settings, ["import_assets", "b_import_assets"], import_assets)
    _set_property(settings, ["landscape_resolution"], landscape_resolution)
    _set_property(settings, ["default_building_height"], default_building_height)
    _set_property(settings, ["road_z_offset"], road_z_offset)
    if asset_dictionary is not None:
        _set_property(settings, ["asset_dictionary"], asset_dictionary)
    return settings


def run_import(
    osm_file_path,
    elevation_data_path="",
    asset_dictionary_path="",
    world_scale=100.0,
    import_terrain=True,
    import_roads=True,
    import_buildings=True,
    import_assets=True,
    landscape_resolution=1009,
    default_building_height=9.0,
    road_z_offset=2.0,
):
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor_subsystem.get_editor_world()
    if world is None:
        raise RuntimeError("Could not resolve the editor world")

    asset_dictionary = None
    if asset_dictionary_path:
        asset_dictionary = unreal.load_asset(asset_dictionary_path)
        if asset_dictionary is None:
            raise RuntimeError("Failed to load asset dictionary: {}".format(asset_dictionary_path))

    settings = _make_settings(
        osm_file_path=osm_file_path,
        elevation_data_path=elevation_data_path,
        world_scale=world_scale,
        import_terrain=import_terrain,
        import_roads=import_roads,
        import_buildings=import_buildings,
        import_assets=import_assets,
        landscape_resolution=landscape_resolution,
        default_building_height=default_building_height,
        road_z_offset=road_z_offset,
        asset_dictionary=asset_dictionary,
    )

    pipeline = unreal.OsmImportPipeline.new_pipeline()
    if pipeline is None:
        raise RuntimeError("Failed to create OsmImportPipeline")

    success = pipeline.execute(world, settings)
    unreal.log("Osm2Map import {}".format("succeeded" if success else "failed"))
    return success


def export_dictionary(asset_dictionary_path, output_json_path):
    dictionary = unreal.load_asset(asset_dictionary_path)
    if dictionary is None:
        raise RuntimeError("Failed to load asset dictionary: {}".format(asset_dictionary_path))
    return dictionary.export_to_json(output_json_path)


def import_dictionary(asset_dictionary_path, input_json_path):
    dictionary = unreal.load_asset(asset_dictionary_path)
    if dictionary is None:
        raise RuntimeError("Failed to load asset dictionary: {}".format(asset_dictionary_path))
    return dictionary.import_from_json(input_json_path)


# Example usage in the UE Python console:
# import osm2map_import
# osm2map_import.run_import(r"C:/Data/city.osm", elevation_data_path=r"C:/Data/srtm", asset_dictionary_path="/Game/Osm/MyDictionary")