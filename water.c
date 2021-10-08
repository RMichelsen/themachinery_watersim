static struct tm_entity_api* tm_entity_api;
static struct tm_transform_component_api* tm_transform_component_api;
static struct tm_temp_allocator_api* tm_temp_allocator_api;
static struct tm_the_truth_api* tm_the_truth_api;
static struct tm_localizer_api* tm_localizer_api;
static struct tm_creation_graph_api* tm_creation_graph_api;
static struct tm_render_graph_module_api *tm_render_graph_module_api;
static struct tm_api_registry_api* tm_global_api_registry;
static struct tm_render_graph_setup_api* tm_render_graph_setup_api;
static struct tm_shader_repository_api* tm_shader_repository_api;
static struct tm_shader_api* tm_shader_api;
static struct tm_render_graph_api* tm_render_graph_api;
static struct tm_render_graph_execute_api* tm_render_graph_execute_api;
static struct tm_renderer_api* tm_renderer_api;
static struct tm_buffer_format_api* tm_buffer_format_api;
static struct tm_random_api* tm_random_api;
static struct tm_creation_graph_interpreter_api* tm_creation_graph_interpreter_api;
static struct tm_os_api* tm_os_api;

#include <plugins/default_render_pipe/default_render_pipe.h>
#include <plugins/entity/entity.h>
#include <plugins/entity/transform_component.h>
#include <plugins/render_graph/render_graph.h>
#include <plugins/renderer/commands.h>
#include <plugins/renderer/render_backend.h>
#include <plugins/renderer/render_command_buffer.h>
#include <plugins/renderer/renderer.h>
#include <plugins/renderer/resources.h>
#include <plugins/render_utilities/render_component.h>
#include <plugins/shader_system/shader_system.h>
#include <plugins/the_machinery_shared/component_interfaces/editor_ui_interface.h>
#include <plugins/the_machinery_shared/render_context.h>
#include <plugins/the_machinery_shared/component_interfaces/shader_interface.h>

#include <plugins/creation_graph/creation_graph.h>
#include <plugins/creation_graph/creation_graph_interpreter.h>
#include <plugins/creation_graph/creation_graph_blackboard.inl>
#include <plugins/creation_graph/creation_graph_output.inl>

#include <foundation/api_registry.h>
#include <foundation/buffer_format.h>
#include <foundation/carray.inl>
#include <foundation/localizer.h>
#include <foundation/math.inl>
#include <foundation/random.h>
#include <foundation/the_truth.h>
#include <foundation/os.h>

#define TM_TT_TYPE__WATER_COMPONENT "tm_water_component"
#define TM_TT_TYPE_HASH__WATER_COMPONENT TM_STATIC_HASH("tm_water_component", 0x39650fe65f4e9eb9ULL)

#define TM_WATER_DISPLACEMENT_MAP_BLACKBOARD_TARGET TM_STATIC_HASH("tm_displacement_map", 0xa29b2265162cb5d9ULL)
#define TM_WATER_NORMAL_MAP_BLACKBOARD_TARGET TM_STATIC_HASH("tm_normal_map", 0x18dd4de8b1ad62e6ULL)

#define WATER_RESOLUTION 1024

typedef struct tm_component_manager_o
{
    tm_entity_context_o *ctx;
    tm_allocator_i allocator;

    tm_renderer_backend_i *rb;

    tm_render_graph_module_o *water_module;

    tm_renderer_handle_t h0tilde_handle;
    tm_renderer_handle_t h0tilde_conj_handle;
    tm_renderer_handle_t unif_randoms_handle;

    tm_renderer_handle_t h0tilde_tx_handle;
    tm_renderer_handle_t h0tilde_ty_handle;
    tm_renderer_handle_t h0tilde_tz_handle;
    tm_renderer_handle_t h0tilde_slope_x_handle;
    tm_renderer_handle_t h0tilde_slope_z_handle;

    tm_renderer_handle_t hor_h0tilde_dx_handle;
    tm_renderer_handle_t hor_h0tilde_dy_handle;
    tm_renderer_handle_t hor_h0tilde_dz_handle;
    tm_renderer_handle_t hor_h0tilde_slope_dx_handle;
    tm_renderer_handle_t hor_h0tilde_slope_dz_handle;

    tm_renderer_handle_t ver_h0tilde_dx_handle;
    tm_renderer_handle_t ver_h0tilde_dy_handle;
    tm_renderer_handle_t ver_h0tilde_dz_handle;
    tm_renderer_handle_t ver_h0tilde_slope_dx_handle;
    tm_renderer_handle_t ver_h0tilde_slope_dz_handle;

    tm_renderer_handle_t displacement_map_handle;
    tm_renderer_handle_t normal_map_handle;

    tm_tt_id_t displacement_map_asset;
    tm_tt_id_t normal_map_asset;

    tm_creation_graph_instance_t creation_graph_instance;
} tm_component_manager_o;


typedef struct tm_water_pass_runtime_data_t
{
    tm_render_graph_handle_t displacement_map_graph_handle;
    tm_render_graph_handle_t normal_map_graph_handle;

    TM_PAD(4);
    tm_shader_o *precalc_shader;
    tm_shader_o *waveheight_shader;
    tm_shader_o *ifft_shader;
    tm_shader_o *displacement_shader;
} tm_water_pass_runtime_data_t;

static inline tm_render_graph_handle_t graph_gpu_resource(tm_render_graph_setup_o* graph_setup, tm_strhash_t resource_name)
{
    tm_render_graph_blackboard_value v;
    if (tm_render_graph_setup_api->read_blackboard(graph_setup, resource_name, &v))
        return (tm_render_graph_handle_t) { v.uint32 };
    else
        return tm_render_graph_setup_api->external_resource(graph_setup, resource_name);
}

static void graph_module_inject(tm_component_manager_o* manager, tm_render_graph_module_o* module)
{
    tm_render_graph_module_api->insert_extension(module, TM_DEFAULT_RENDER_PIPE_MAIN_EXTENSION_GBUFFER, manager->water_module, 1.0f);
}

static const char *component__category(void)
{
    return TM_LOCALIZE("");
}

static tm_ci_editor_ui_i *editor_aspect = &(tm_ci_editor_ui_i){
    .category = component__category
};

static tm_ci_shader_i *shader_aspect = &(tm_ci_shader_i){
    .graph_module_inject = graph_module_inject
};

enum {
    TM_TT_PROP__WATER_DISPLACEMENT_MAP,
    TM_TT_PROP__WATER_NORMAL_MAP
};

static void truth__create_types(struct tm_the_truth_o* tt)
{
    tm_the_truth_property_definition_t component_properties[] = {
        [TM_TT_PROP__WATER_DISPLACEMENT_MAP] = { "water_displacement_map", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH },
        [TM_TT_PROP__WATER_NORMAL_MAP] = { "water_normal_map", TM_THE_TRUTH_PROPERTY_TYPE_SUBOBJECT, .type_hash = TM_TT_TYPE_HASH__CREATION_GRAPH }
    };

    const tm_tt_type_t component_type = tm_the_truth_api->create_object_type(tt, TM_TT_TYPE__WATER_COMPONENT, 
        component_properties, TM_ARRAY_COUNT(component_properties));
    const tm_tt_id_t component = tm_the_truth_api->create_object_of_type(
        tt, tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__WATER_COMPONENT), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_object_o *component_w = tm_the_truth_api->write(tt, component);

    tm_creation_graph_api->create_truth_types(tt);
    tm_the_truth_api->set_default_object_to_create_subobjects(tt, component_type);

    const tm_tt_id_t displacement_image_object = 
        tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__CREATION_GRAPH), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_subobject_id(tt, component_w, TM_TT_PROP__WATER_DISPLACEMENT_MAP, displacement_image_object, TM_TT_NO_UNDO_SCOPE);

    const tm_tt_id_t normal_image_object = 
        tm_the_truth_api->create_object_of_type(tt, tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__CREATION_GRAPH), TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_subobject_id(tt, component_w, TM_TT_PROP__WATER_NORMAL_MAP, normal_image_object, TM_TT_NO_UNDO_SCOPE);

    tm_the_truth_api->commit(tt, component_w, TM_TT_NO_UNDO_SCOPE);
    tm_the_truth_api->set_default_object(tt, component_type, component);

    tm_the_truth_api->set_aspect(tt, component_type, TM_CI_SHADER, shader_aspect);
    tm_the_truth_api->set_aspect(tt, component_type, TM_CI_EDITOR_UI, editor_aspect);
}

static void water_pass_init(void* const_data, tm_allocator_i* allocator, tm_renderer_resource_command_buffer_o* res_buf)
{
    tm_component_manager_o *manager = *(tm_component_manager_o **)const_data;

    if (!manager->unif_randoms_handle.resource)
    {
        const tm_renderer_buffer_desc_t unif_randoms_buffer_desc = {
            .size = WATER_RESOLUTION * WATER_RESOLUTION * sizeof(float) * 4,
            .usage_flags = TM_RENDERER_BUFFER_USAGE_STORAGE | TM_RENDERER_BUFFER_USAGE_UPDATABLE,
            .debug_tag = "unif_randoms_data"
        };

        float* unif_randoms_data = 0;
        manager->unif_randoms_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->map_create_buffer(
            res_buf, &unif_randoms_buffer_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, 0, (void**)&unif_randoms_data);

        for (int i = 0; i < WATER_RESOLUTION * WATER_RESOLUTION * 4; ++i)
        {
            unif_randoms_data[i] = tm_random_float(0.0, 1.0);
        }
    }

    const uint32_t format32323232 = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 32, 32, 32, 32);
    tm_renderer_image_desc_t image_desc = {
            .type = TM_RENDERER_IMAGE_TYPE_2D,
            .usage_flags = TM_RENDERER_IMAGE_USAGE_UAV,
            .format = format32323232,
            .width = WATER_RESOLUTION,
            .height = WATER_RESOLUTION,
            .depth = 1,
            .mip_levels = 1,
            .layer_count = 1,
            .sample_count = 1,
            .debug_tag = "h0tilde"
    };

    if (!manager->h0tilde_handle.resource)
        manager->h0tilde_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    image_desc.debug_tag = "h0tilde_conj";
    if (!manager->h0tilde_conj_handle.resource)
        manager->h0tilde_conj_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    image_desc.debug_tag = "h0tilde_tx";
    if (!manager->h0tilde_tx_handle.resource)
        manager->h0tilde_tx_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "h0tilde_ty";
    if (!manager->h0tilde_ty_handle.resource)
        manager->h0tilde_ty_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "h0tilde_tz";
    if (!manager->h0tilde_tz_handle.resource)
        manager->h0tilde_tz_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "h0tilde_slope_x";
    if (!manager->h0tilde_slope_x_handle.resource)
        manager->h0tilde_slope_x_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "h0tilde_slope_z";
    if (!manager->h0tilde_slope_z_handle.resource)
        manager->h0tilde_slope_z_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    image_desc.debug_tag = "hor_h0tilde_dx";
    if (!manager->hor_h0tilde_dx_handle.resource)
        manager->hor_h0tilde_dx_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "hor_h0tilde_dy";
    if (!manager->hor_h0tilde_dy_handle.resource)
        manager->hor_h0tilde_dy_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "hor_h0tilde_dz";
    if (!manager->hor_h0tilde_dz_handle.resource)
        manager->hor_h0tilde_dz_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "hor_h0tilde_slope_dx";
    if (!manager->hor_h0tilde_slope_dx_handle.resource)
        manager->hor_h0tilde_slope_dx_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "hor_h0tilde_slope_dz";
    if (!manager->hor_h0tilde_slope_dz_handle.resource)
        manager->hor_h0tilde_slope_dz_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    image_desc.debug_tag = "ver_h0tilde_dx";
    if (!manager->ver_h0tilde_dx_handle.resource)
        manager->ver_h0tilde_dx_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "ver_h0tilde_dy";
    if (!manager->ver_h0tilde_dy_handle.resource)
        manager->ver_h0tilde_dy_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "ver_h0tilde_dz";
    if (!manager->ver_h0tilde_dz_handle.resource)
        manager->ver_h0tilde_dz_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "ver_h0tilde_slope_dx";
    if (!manager->ver_h0tilde_slope_dx_handle.resource)
        manager->ver_h0tilde_slope_dx_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "ver_h0tilde_slope_dz";
    if (!manager->ver_h0tilde_slope_dz_handle.resource)
        manager->ver_h0tilde_slope_dz_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);

    image_desc.debug_tag = "displacement_map";
    if (!manager->displacement_map_handle.resource)
        manager->displacement_map_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
    image_desc.debug_tag = "normal_map";
    if (!manager->normal_map_handle.resource)
        manager->normal_map_handle = tm_renderer_api->tm_renderer_resource_command_buffer_api->create_image(res_buf,
            &image_desc, TM_RENDERER_DEVICE_AFFINITY_MASK_ALL);
}

static void water_pass_setup(const void* const_data, void* runtime_data, tm_render_graph_setup_o* graph_setup)
{
    tm_water_pass_runtime_data_t *rdata = (tm_water_pass_runtime_data_t*)runtime_data;

    rdata->displacement_map_graph_handle = graph_gpu_resource(graph_setup, TM_WATER_DISPLACEMENT_MAP_BLACKBOARD_TARGET);
    rdata->normal_map_graph_handle = graph_gpu_resource(graph_setup, TM_WATER_NORMAL_MAP_BLACKBOARD_TARGET);

    tm_render_graph_setup_api->set_active(graph_setup, true);
    tm_render_graph_setup_api->set_output(graph_setup, true);

    rdata->precalc_shader = tm_shader_repository_api->lookup_shader(
        tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("water_precalc", 0xaa8368b1ba2eacaaULL));

    rdata->waveheight_shader = tm_shader_repository_api->lookup_shader(
        tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("water_waveheight", 0x451d10121080aadULL));

    rdata->ifft_shader = tm_shader_repository_api->lookup_shader(
        tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("water_ifft", 0x75cbcaa28183173cULL));

    rdata->displacement_shader = tm_shader_repository_api->lookup_shader(
        tm_render_graph_setup_api->shader_repository(graph_setup), TM_STATIC_HASH("water_displacement", 0x74e71a85599fbdf6ULL));

    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->displacement_map_graph_handle, TM_RENDER_GRAPH_WRITE_BIND_FLAG_UAV,
        TM_RENDERER_RESOURCE_STATE_UAV | TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER, TM_RENDERER_RESOURCE_LOAD_OP_DISCARD, 0, (tm_strhash_t) { 0 }, 0);

    tm_render_graph_setup_api->write_gpu_resource(graph_setup, rdata->normal_map_graph_handle, TM_RENDER_GRAPH_WRITE_BIND_FLAG_UAV,
        TM_RENDERER_RESOURCE_STATE_UAV | TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER, TM_RENDERER_RESOURCE_LOAD_OP_DISCARD, 0, (tm_strhash_t) { 0 }, 0);
}

void execute_precalc_shader(tm_water_pass_runtime_data_t* rdata, tm_component_manager_o* manager, 
    uint64_t* sort_key, tm_render_graph_execute_o *graph_execute)
{
    tm_renderer_resource_command_buffer_o* res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);
    tm_renderer_command_buffer_o* cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
    const tm_shader_system_context_o* context = tm_render_graph_execute_api->shader_context(graph_execute);
    tm_shader_io_o* io = tm_shader_api->shader_io(rdata->precalc_shader);

    tm_shader_resource_binder_instance_t rbinder = { 0 };
    tm_shader_api->create_resource_binder_instances(io, 1, &rbinder);

    tm_shader_resource_update_t res_update = {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde", 0x60d4bcd4195b12b7ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_conj_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_conj", 0x58e8a3eee09fdaadULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->unif_randoms_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("unif_randoms", 0x3c6287dd30f2dbe7ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    tm_shader_constant_buffer_instance_t cbuffer;
    tm_shader_api->create_constant_buffer_instances(io, 1, &cbuffer);

    float W[2] = { 18.0f, 24.0f };
    tm_shader_constant_update_t const_update = {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(float) * 2,
        .data = W
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("W", 0x97a195782b82dd4fULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    int N = WATER_RESOLUTION;
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(int),
        .data = &N
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("N", 0xf1da4bb9c1af9169ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    float A = 12000.0f;
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(float),
        .data = &A
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("A", 0x37150ad24f8a8007ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    int L = 1000;
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(int),
        .data = &L
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("L", 0x2c8a471e0255e301ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    tm_renderer_shader_info_t shader_info;
    if (tm_shader_api->assemble_shader_infos(rdata->precalc_shader, NULL, 0, context, TM_STRHASH(0), res_buf, &cbuffer, &rbinder, 1, &shader_info))
    {
        const tm_renderer_compute_info_t dispatch_info = {
            .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL,
            .dispatch.group_count = { WATER_RESOLUTION / 32, WATER_RESOLUTION / 32, 1 }
        };

        tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, sort_key, &dispatch_info, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, &cbuffer, 1);
    tm_shader_api->destroy_resource_binder_instances(io, &rbinder, 1);
}

void execute_waveheight_shader(tm_water_pass_runtime_data_t* rdata, tm_component_manager_o* manager,
    uint64_t* sort_key, tm_render_graph_execute_o* graph_execute)
{
    tm_renderer_resource_command_buffer_o* res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);
    tm_renderer_command_buffer_o* cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
    const tm_shader_system_context_o* context = tm_render_graph_execute_api->shader_context(graph_execute);
    tm_shader_io_o *io = tm_shader_api->shader_io(rdata->waveheight_shader);

    tm_shader_resource_binder_instance_t rbinder = {0};
    tm_shader_api->create_resource_binder_instances(io, 1, &rbinder);

    tm_shader_resource_update_t res_update = {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde", 0x60d4bcd4195b12b7ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_conj_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_conj", 0x58e8a3eee09fdaadULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_tx_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_tx", 0xeb72b8c780396bffULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_ty_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_ty", 0x41568d4ed8085fa7ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_tz_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_tz", 0x6a53c2e4e871a543ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_slope_x_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_slope_x", 0x9fc81110070d2c5aULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->h0tilde_slope_z_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_slope_z", 0x78498479ed9f42bdULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    tm_shader_constant_buffer_instance_t cbuffer;
    tm_shader_api->create_constant_buffer_instances(io, 1, &cbuffer);

    int N = WATER_RESOLUTION;
    tm_shader_constant_update_t const_update = {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(int),
        .data = &N
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("N", 0xf1da4bb9c1af9169ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    int L = 1000;
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(int),
        .data = &L
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("L", 0x2c8a471e0255e301ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    float RT = 200.0f;
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(float),
        .data = &RT
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("RT", 0x633baf510835213ULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);

    static float T = 0.0f;
    static tm_clock_o before = { 0 };
    const_update = (tm_shader_constant_update_t) {
        .instance_id = cbuffer.instance_id,
        .num_bytes = sizeof(float),
        .data = &T
    };
    tm_shader_api->lookup_constant(io, TM_STATIC_HASH("T", 0x7a307fb51b9741cdULL), NULL, &const_update.constant_offset);
    tm_shader_api->update_constants(io, res_buf, &const_update, 1);
    if (before.opaque == 0)
    {
        before = tm_os_api->time->now();
    }
    else
    {
        tm_clock_o now = tm_os_api->time->now();
        T += (float)tm_os_api->time->delta(now, before);
        before = now;
    }

    tm_renderer_shader_info_t shader_info;
    if (tm_shader_api->assemble_shader_infos(rdata->waveheight_shader, NULL, 0, context, TM_STRHASH(0), res_buf, &cbuffer, &rbinder, 1, &shader_info))
    {
        const tm_renderer_compute_info_t dispatch_info = {
            .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL,
            .dispatch.group_count = { WATER_RESOLUTION / 32, WATER_RESOLUTION / 32, 1 }
        };

        tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, sort_key, &dispatch_info, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, &cbuffer, 1);
    tm_shader_api->destroy_resource_binder_instances(io, &rbinder, 1);
}

void execute_horizontal_ifft(tm_water_pass_runtime_data_t* rdata, tm_component_manager_o* manager,
    uint64_t* sort_key, tm_render_graph_execute_o* graph_execute)
{
    tm_renderer_resource_command_buffer_o* res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);
    tm_renderer_command_buffer_o* cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
    const tm_shader_system_context_o* context = tm_render_graph_execute_api->shader_context(graph_execute);
    tm_shader_io_o* io = tm_shader_api->shader_io(rdata->ifft_shader);

    tm_shader_resource_binder_instance_t rbinder[5] = { 0 };
    tm_shader_constant_buffer_instance_t cbuffer[5] = { 0 };

    tm_renderer_handle_t waveheight_images[] =
    {
        manager->h0tilde_tx_handle,
        manager->h0tilde_ty_handle,
        manager->h0tilde_tz_handle,
        manager->h0tilde_slope_x_handle,
        manager->h0tilde_slope_z_handle
    };
    tm_renderer_handle_t horizontal_images[] =
    {
        manager->hor_h0tilde_dx_handle,
        manager->hor_h0tilde_dy_handle,
        manager->hor_h0tilde_dz_handle,
        manager->hor_h0tilde_slope_dx_handle,
        manager->hor_h0tilde_slope_dz_handle,
    };

    for (int i = 0; i < 5; ++i)
    {
        tm_shader_api->create_resource_binder_instances(io, 1, &rbinder[i]);
        tm_shader_api->create_constant_buffer_instances(io, 1, &cbuffer[i]);

        tm_shader_resource_update_t res_update = {
            .instance_id = rbinder[i].instance_id,
            .num_resources = 1,
            .resources = &waveheight_images[i]
        };
        tm_shader_api->lookup_resource(io, TM_STATIC_HASH("input_image", 0xf7aafe734b73d1adULL), NULL, &res_update.resource_slot);
        tm_shader_api->update_resources(io, res_buf, &res_update, 1);

        res_update = (tm_shader_resource_update_t) {
            .instance_id = rbinder[i].instance_id,
            .num_resources = 1,
            .resources = &horizontal_images[i]
        };
        tm_shader_api->lookup_resource(io, TM_STATIC_HASH("output_image", 0x3b9e940ce15a58bULL), NULL, &res_update.resource_slot);
        tm_shader_api->update_resources(io, res_buf, &res_update, 1);

        int vertical_pass = 0;
        tm_shader_constant_update_t const_update = {
            .instance_id = cbuffer[i].instance_id,
            .num_bytes = sizeof(int),
            .data = &vertical_pass
        };
        tm_shader_api->lookup_constant(io, TM_STATIC_HASH("vertical_pass", 0x7e9962791cf825b1ULL), NULL, &const_update.constant_offset);
        tm_shader_api->update_constants(io, res_buf, &const_update, 1);

        tm_renderer_shader_info_t shader_info;
        if (tm_shader_api->assemble_shader_infos(rdata->ifft_shader, NULL, 0, context, TM_STRHASH(0), res_buf, &cbuffer[i], &rbinder[i], 1, &shader_info))
        {
            const tm_renderer_compute_info_t dispatch_info = {
                .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL,
                .dispatch.group_count = { 1, WATER_RESOLUTION, 1 }
            };

            tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, sort_key, &dispatch_info, &shader_info, 1);
        }
    }

    for (int i = 0; i < 5; ++i)
    {
        tm_shader_api->destroy_constant_buffer_instances(io, &cbuffer[i], 1);
        tm_shader_api->destroy_resource_binder_instances(io, &rbinder[i], 1);
    }
}

void execute_vertical_ifft(tm_water_pass_runtime_data_t* rdata, tm_component_manager_o* manager,
    uint64_t* sort_key, tm_render_graph_execute_o* graph_execute)
{
    tm_renderer_resource_command_buffer_o* res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);
    tm_renderer_command_buffer_o* cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
    const tm_shader_system_context_o* context = tm_render_graph_execute_api->shader_context(graph_execute);
    tm_shader_io_o* io = tm_shader_api->shader_io(rdata->ifft_shader);

    tm_shader_resource_binder_instance_t rbinder[5] = { 0 };
    tm_shader_constant_buffer_instance_t cbuffer[5] = { 0 };
    tm_renderer_handle_t horizontal_images[] =
    {
        manager->hor_h0tilde_dx_handle,
        manager->hor_h0tilde_dy_handle,
        manager->hor_h0tilde_dz_handle,
        manager->hor_h0tilde_slope_dx_handle,
        manager->hor_h0tilde_slope_dz_handle
    };

    tm_renderer_handle_t vertical_images[] =
    {
        manager->ver_h0tilde_dx_handle,
        manager->ver_h0tilde_dy_handle,
        manager->ver_h0tilde_dz_handle,
        manager->ver_h0tilde_slope_dx_handle,
        manager->ver_h0tilde_slope_dz_handle
    };

    for (int i = 0; i < 5; ++i)
    {
        tm_shader_api->create_resource_binder_instances(io, 1, &rbinder[i]);
        tm_shader_api->create_constant_buffer_instances(io, 1, &cbuffer[i]);

        tm_shader_resource_update_t res_update = {
            .instance_id = rbinder[i].instance_id,
            .num_resources = 1,
            .resources = &horizontal_images[i]
        };
        tm_shader_api->lookup_resource(io, TM_STATIC_HASH("input_image", 0xf7aafe734b73d1adULL), NULL, &res_update.resource_slot);
        tm_shader_api->update_resources(io, res_buf, &res_update, 1);

        res_update = (tm_shader_resource_update_t) {
            .instance_id = rbinder[i].instance_id,
            .num_resources = 1,
            .resources = &vertical_images[i]
        };
        tm_shader_api->lookup_resource(io, TM_STATIC_HASH("output_image", 0x3b9e940ce15a58bULL), NULL, &res_update.resource_slot);
        tm_shader_api->update_resources(io, res_buf, &res_update, 1);

        int vertical_pass = 1;
        tm_shader_constant_update_t const_update = {
            .instance_id = cbuffer[i].instance_id,
            .num_bytes = sizeof(int),
            .data = &vertical_pass
        };
        tm_shader_api->lookup_constant(io, TM_STATIC_HASH("vertical_pass", 0x7e9962791cf825b1ULL), NULL, &const_update.constant_offset);
        tm_shader_api->update_constants(io, res_buf, &const_update, 1);

        tm_renderer_shader_info_t shader_info;
        if (tm_shader_api->assemble_shader_infos(rdata->ifft_shader, NULL, 0, context, TM_STRHASH(0), res_buf, &cbuffer[i], &rbinder[i], 1, &shader_info))
        {
            const tm_renderer_compute_info_t dispatch_info = {
                .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL,
                .dispatch.group_count = { 1, WATER_RESOLUTION, 1 }
            };

            tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, sort_key, &dispatch_info, &shader_info, 1);
        }
    }

    for (int i = 0; i < 5; ++i)
    {
        tm_shader_api->destroy_constant_buffer_instances(io, &cbuffer[i], 1);
        tm_shader_api->destroy_resource_binder_instances(io, &rbinder[i], 1);
    }
}

void execute_displacement_shader(tm_water_pass_runtime_data_t* rdata, tm_component_manager_o* manager,
    uint64_t* sort_key, tm_render_graph_execute_o* graph_execute)
{
    tm_renderer_resource_command_buffer_o* res_buf = tm_render_graph_execute_api->default_resource_command_buffer(graph_execute);
    tm_renderer_command_buffer_o* cmd_buf = tm_render_graph_execute_api->default_command_buffer(graph_execute);
    const tm_shader_system_context_o* context = tm_render_graph_execute_api->shader_context(graph_execute);
    tm_shader_io_o* io = tm_shader_api->shader_io(rdata->displacement_shader);

    tm_shader_resource_binder_instance_t rbinder = { 0 };
    tm_shader_constant_buffer_instance_t cbuffer = { 0 };
    tm_shader_api->create_resource_binder_instances(io, 1, &rbinder);
    tm_shader_api->create_constant_buffer_instances(io, 1, &cbuffer);

    tm_shader_resource_update_t res_update = {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->ver_h0tilde_dx_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_dx", 0x80e1afa43a298748ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->ver_h0tilde_dy_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_dy", 0x2772124df52efb5ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->ver_h0tilde_dz_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_dz", 0x2738542b433211f5ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->ver_h0tilde_slope_dx_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_slope_dx", 0x8b080b319e54f2baULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->ver_h0tilde_slope_dz_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("h0tilde_slope_dz", 0x6436b04ed5c293a8ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->displacement_map_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("displacement_map", 0x134f5d4a6d3f822cULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    res_update = (tm_shader_resource_update_t) {
        .instance_id = rbinder.instance_id,
        .num_resources = 1,
        .resources = &manager->normal_map_handle
    };
    tm_shader_api->lookup_resource(io, TM_STATIC_HASH("normal_map", 0xf5c97d31c5c8a1e1ULL), NULL, &res_update.resource_slot);
    tm_shader_api->update_resources(io, res_buf, &res_update, 1);

    tm_renderer_shader_info_t shader_info;
    if (tm_shader_api->assemble_shader_infos(rdata->displacement_shader, NULL, 0, context, TM_STRHASH(0), res_buf, &cbuffer, &rbinder, 1, &shader_info))
    {
        const tm_renderer_compute_info_t dispatch_info = {
            .dispatch_type = TM_RENDERER_DISPATCH_TYPE_NORMAL,
            .dispatch.group_count = { WATER_RESOLUTION / 32, WATER_RESOLUTION / 32, 1 }
        };

        tm_renderer_api->tm_renderer_command_buffer_api->compute_dispatches(cmd_buf, sort_key, &dispatch_info, &shader_info, 1);
    }

    tm_shader_api->destroy_constant_buffer_instances(io, &cbuffer, 1);
    tm_shader_api->destroy_resource_binder_instances(io, &rbinder, 1);
}

static void water_pass_execute(const void* const_data, void* runtime_data, uint64_t sort_key, tm_render_graph_execute_o* graph_execute)
{
    tm_water_pass_runtime_data_t* rdata = (tm_water_pass_runtime_data_t*)runtime_data;
    tm_component_manager_o* manager = *(tm_component_manager_o**)const_data;

    execute_precalc_shader(rdata, manager, &sort_key, graph_execute);
    execute_waveheight_shader(rdata, manager, &sort_key, graph_execute);

    execute_horizontal_ifft(rdata, manager, &sort_key, graph_execute);
    execute_vertical_ifft(rdata, manager, &sort_key, graph_execute);

    execute_displacement_shader(rdata, manager, &sort_key, graph_execute);
}

static void component__destroy(tm_component_manager_o* manager)
{
    tm_renderer_backend_i* rb = manager->rb;
    tm_renderer_resource_command_buffer_o* res_buf;
    rb->create_resource_command_buffers(rb->inst, &res_buf, 1);

    tm_render_graph_module_api->destroy(manager->water_module, res_buf);

    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_conj_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_tx_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_ty_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_tz_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_slope_x_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->h0tilde_slope_z_handle);

    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->hor_h0tilde_dx_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->hor_h0tilde_dy_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->hor_h0tilde_dz_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->hor_h0tilde_slope_dx_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->hor_h0tilde_slope_dz_handle);

    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->ver_h0tilde_dx_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->ver_h0tilde_dy_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->ver_h0tilde_dz_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->ver_h0tilde_slope_dx_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->ver_h0tilde_slope_dz_handle);

    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->displacement_map_handle);
    tm_renderer_api->tm_renderer_resource_command_buffer_api->destroy_resource(res_buf, manager->normal_map_handle);

    rb->submit_resource_command_buffers(rb->inst, &res_buf, 1);
    rb->destroy_resource_command_buffers(rb->inst, &res_buf, 1);

    tm_entity_context_o* ctx = manager->ctx;
    tm_allocator_i allocator = manager->allocator;
    tm_free(&allocator, manager, sizeof(tm_component_manager_o));
    tm_entity_api->destroy_child_allocator(ctx, &allocator);
}

static bool component__load_asset(tm_component_manager_o* manager, struct tm_entity_commands_o* commands, 
    tm_entity_t e, void* data, const tm_the_truth_o* tt, tm_tt_id_t asset)
{
    TM_INIT_TEMP_ALLOCATOR(ta);

    const uint32_t format32323232 = tm_buffer_format_api->encode_uncompressed_format(TM_BUFFER_COMPONENT_TYPE_FLOAT, true, 32, 32, 32, 32);
    tm_renderer_image_desc_t image_desc = {
            .type = TM_RENDERER_IMAGE_TYPE_2D,
            .usage_flags = TM_RENDERER_IMAGE_USAGE_UAV,
            .format = format32323232,
            .width = WATER_RESOLUTION,
            .height = WATER_RESOLUTION,
            .depth = 1,
            .mip_levels = 1,
            .layer_count = 1,
            .sample_count = 1,
    };

    tm_creation_graph_blackboard_value_t blackboard_value = {
        .type = TM_IMAGE_BLACKBOARD_TYPE_IMAGE, .image = {
            .handle = manager->displacement_map_handle,
            .desc = image_desc,
            .resource_state = TM_RENDERER_RESOURCE_STATE_COMPUTE_SHADER |
            TM_RENDERER_RESOURCE_STATE_UAV 
        },
        .validity_hash = 0
    };

    const tm_tt_id_t owner = tm_the_truth_api->owner(tt, asset);
    const tm_the_truth_object_o* owner_obj = tm_tt_read(tt, owner);
    const tm_tt_type_t render_component_type = tm_the_truth_api->object_type_from_name_hash(tt, TM_TT_TYPE_HASH__RENDER_COMPONENT);
    const tm_tt_id_t render_component = tm_the_truth_api->find_subobject_of_type(tt, owner_obj, TM_TT_PROP__ENTITY__COMPONENTS, render_component_type);
    const tm_the_truth_object_o* render_component_obj = tm_tt_read(tt, render_component);
    const tm_tt_id_t render_component_cg = tm_the_truth_api->get_subobject(tt, render_component_obj, TM_TT_PROP__RENDER_COMPONENT__CREATION_GRAPH);

    tm_creation_graph_context_t cg_ctx = (tm_creation_graph_context_t){ .rb = manager->rb, 
        .device_affinity_mask = TM_RENDERER_DEVICE_AFFINITY_MASK_ALL, .tt = (tm_the_truth_o *)tt };

    if (!manager->creation_graph_instance.graph.u64 || tm_the_truth_api->version(tt, render_component_cg) != manager->creation_graph_instance.version)
        manager->creation_graph_instance = tm_creation_graph_api->create_instance((tm_the_truth_o*)tt, render_component_cg, &cg_ctx);

    tm_creation_graph_api->write_blackboard((tm_the_truth_o *)tt, render_component_cg, TM_WATER_DISPLACEMENT_MAP_BLACKBOARD_TARGET, &blackboard_value);
    tm_creation_graph_blackboard_value_t* value = tm_creation_graph_interpreter_api->write_variable(
        &manager->creation_graph_instance, TM_WATER_DISPLACEMENT_MAP_BLACKBOARD_TARGET, 1, sizeof(tm_creation_graph_blackboard_value_t));
    *value = blackboard_value;

    blackboard_value.image.handle = manager->normal_map_handle;
    tm_creation_graph_api->write_blackboard((tm_the_truth_o *)tt, render_component_cg, TM_WATER_NORMAL_MAP_BLACKBOARD_TARGET, &blackboard_value);
    value = tm_creation_graph_interpreter_api->write_variable(
        &manager->creation_graph_instance, TM_WATER_NORMAL_MAP_BLACKBOARD_TARGET, 1, sizeof(tm_creation_graph_blackboard_value_t));
    *value = blackboard_value;

    TM_SHUTDOWN_TEMP_ALLOCATOR(ta);
    return true;
}

static void component__create(tm_entity_context_o* ctx)
{
    tm_renderer_backend_i* backend = tm_first_implementation(tm_global_api_registry, tm_renderer_backend_i);
    if (!backend)
        return;

    tm_allocator_i a;
    tm_entity_api->create_child_allocator(ctx, TM_TT_TYPE__WATER_COMPONENT, &a);
    tm_component_manager_o* manager = tm_alloc(&a, sizeof(*manager));
    *manager = (tm_component_manager_o) {
        .ctx = ctx,
        .allocator = a,
        .rb = backend,
    };
    manager->water_module = tm_render_graph_module_api->create(&manager->allocator, "Water Generation");
    tm_render_graph_module_api->add_pass(manager->water_module, &(tm_render_graph_pass_i){
        .api = {
            .init_pass = water_pass_init,
            .setup_pass = water_pass_setup,
            .execute_pass = water_pass_execute
        },
        .const_data_size = sizeof(tm_component_manager_o **),
        .const_data = &manager,
        .runtime_data_size = sizeof(tm_water_pass_runtime_data_t),
        .profiling_scope = "Water Generation"
    });

    tm_component_i component = {
        .name = TM_TT_TYPE__WATER_COMPONENT,
        .destroy = component__destroy,
        .load_asset = component__load_asset,
        .manager = manager
    };

    tm_entity_api->register_component(ctx, &component);
}

TM_DLL_EXPORT void tm_load_plugin(struct tm_api_registry_api* reg, bool load)
{
    tm_entity_api = tm_get_api(reg, tm_entity_api);
    tm_transform_component_api = tm_get_api(reg, tm_transform_component_api);
    tm_the_truth_api = tm_get_api(reg, tm_the_truth_api);
    tm_temp_allocator_api = tm_get_api(reg, tm_temp_allocator_api);
    tm_localizer_api = tm_get_api(reg, tm_localizer_api);
    tm_creation_graph_api = tm_get_api(reg, tm_creation_graph_api);
    tm_render_graph_module_api = tm_get_api(reg, tm_render_graph_module_api);
    tm_global_api_registry = tm_get_api(reg, tm_api_registry_api);
    tm_render_graph_setup_api = tm_get_api(reg, tm_render_graph_setup_api);
    tm_shader_repository_api = tm_get_api(reg, tm_shader_repository_api);
    tm_shader_api = tm_get_api(reg, tm_shader_api);
    tm_render_graph_api = tm_get_api(reg, tm_render_graph_api);
    tm_render_graph_execute_api = tm_get_api(reg, tm_render_graph_execute_api);
    tm_renderer_api = tm_get_api(reg, tm_renderer_api);
    tm_buffer_format_api = tm_get_api(reg, tm_buffer_format_api);
    tm_random_api = tm_get_api(reg, tm_random_api);
    tm_creation_graph_interpreter_api = tm_get_api(reg, tm_creation_graph_interpreter_api);
    tm_os_api = tm_get_api(reg, tm_os_api);

    tm_add_or_remove_implementation(reg, load, tm_the_truth_create_types_i, truth__create_types);
    tm_add_or_remove_implementation(reg, load, tm_entity_create_component_i, component__create);
}

