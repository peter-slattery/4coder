/* Modal Requirements:
  *  1. Store multiple keymaps (more than 2)
*  2. Switch between active keymaps
*       Specifically avoid every keypress checking the current mode
*       Two kinds of switching:
*         - go to last keybinding (ping pong)
*         - go to next keybinding
*  3. Cursor color changes based on mode
*  4. Display keybindings for current keymap
*/

/* Design Thoughts:
*  this file shouldn't actually create the modal system, just provide tools
*  for doing so
*/

struct GS_Mode
{
  Mapping map;
  FColor cursor_color;
  String_Const_u8 name;
};

// Modal System maps
global String_ID gs_modal_map_id_global;
global String_ID gs_modal_map_id_file;
global String_ID gs_modal_map_id_code;

// Modal system modes
global GS_Mode* gs_modal_modes;
global u32      gs_modal_modes_cap;

// State tracking
global u32 gs_modal_last_mode;
global u32 gs_modal_curr_mode;

typedef CUSTOM_COMMAND_SIG(gs_custom_cmd);

function void
gs_modal_init(u32 mode_cap, Thread_Context* tctx)
{
  Assert(mode_cap > 0);
  gs_modal_modes_cap = mode_cap;
  gs_modal_modes = base_array(tctx->allocator, GS_Mode, mode_cap);
  
  gs_modal_map_id_global = vars_save_string_lit("keys_global");
  gs_modal_map_id_file   = vars_save_string_lit("keys_file");
  gs_modal_map_id_code   = vars_save_string_lit("keys_code");
  
  for (u32 i = 0; i < gs_modal_modes_cap; i++)
  {
    GS_Mode* mode = gs_modal_modes + i;
    mapping_init(tctx, &mode->map);
    gs_setup_essential_mapping(&mode->map, gs_modal_map_id_global, gs_modal_map_id_file, gs_modal_map_id_code);
  }
}

function GS_Mode*
gs_modal_get_mode(u32 mode_id)
{
  GS_Mode* result = gs_modal_modes + mode_id;
  return result;
}

function void
gs_modal_bind(Mapping* mode, Command_Map* map, gs_custom_cmd* proc, u32 key_code, u32 mod_key_code0, u32 mod_key_code1)
{
  map_set_binding_l(mode, map, BindFWrap_(proc), InputEventKind_KeyStroke, key_code, mod_key_code0, mod_key_code1, 0);
}

function void
gs_modal_bind(u32 mode_id, String_ID map_id, gs_custom_cmd* proc, u32 key_code, u32 mod_key_code0, u32 mod_key_code1)
{
  Assert(mode_id < gs_modal_modes_cap);
  
  GS_Mode* mode = gs_modal_get_mode(mode_id);
  Mapping* m = &mode->map;
  Command_Map* map = mapping_get_or_make_map(m, map_id);
  
  gs_modal_bind(m, map, proc, key_code, mod_key_code0, mod_key_code1);
}

function void
gs_modal_bind_all(String_ID map_id, gs_custom_cmd* proc, u32 key_code, u32 mod_key_code0, u32 mod_key_code1)
{
  for (u32 i = 0; i < gs_modal_modes_cap; i++)
  {
    Mapping* m = &gs_modal_modes[i].map;
    Command_Map* map = mapping_get_or_make_map(m, map_id);
    gs_modal_bind(m, map, proc, key_code, mod_key_code0, mod_key_code1);
  }
}

function GS_Mode*
gs_modal_get_mode_curr()
{
  return gs_modal_get_mode(gs_modal_curr_mode);
}

function void
gs_modal_set_mode(u32 mode_id)
{
  gs_modal_last_mode = gs_modal_curr_mode;
  gs_modal_curr_mode = mode_id;
}

function u32
gs_modal_get_next_mode(u32 base)
{
  u32 result = (base + 1) % gs_modal_modes_cap;
  return result;
}

function void
gs_modal_set_cursor_color(u32 mode_id, FColor color)
{
  GS_Mode* mode = gs_modal_get_mode(mode_id);
  mode->cursor_color = color;
}

function void
gs_modal_set_cursor_color_u32(u32 mode_id, u32 color)
{
  FColor fc = {};
  fc.argb = color;
  gs_modal_set_cursor_color(mode_id, fc);
}

CUSTOM_COMMAND_SIG(gs_modal_set_mode_toggle)
{
  u32 next_mode = gs_modal_last_mode;
  if (next_mode == gs_modal_curr_mode)
  {
    next_mode = gs_modal_get_next_mode(next_mode);
  }
  gs_modal_set_mode(next_mode);
}

CUSTOM_COMMAND_SIG(gs_modal_set_mode_next)
{
  u32 next_mode = gs_modal_get_next_mode(gs_modal_curr_mode);
  gs_modal_set_mode(next_mode);
}