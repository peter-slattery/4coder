function void
gs_input_handler_init(Application_Links *app, Arena *arena){
  Thread_Context *tctx = get_thread_context(app);
  
  View_ID view = get_this_ctx_view(app, Access_Always);
  String_Const_u8 name = push_u8_stringf(arena, "view %d", view);
  
  Profile_Global_List *list = get_core_profile_list(app);
  ProfileThreadName(tctx, list, name);
  
  View_Context ctx = view_current_context(app, view);
  ctx.mapping = &gs_modal_get_mode_curr()->map;
  ctx.map_id = vars_save_string_lit("keys_global");
  view_alter_context(app, view, &ctx);
}

function Implicit_Map_Result
gs_implicit_map(Application_Links *app, String_ID lang, String_ID mode_id, Input_Event *event)
{
  Implicit_Map_Result result = {};
  
  View_ID view = get_this_ctx_view(app, Access_Always);
  
  Command_Map_ID map_id = default_get_map_id(app, view);
  GS_Mode* mode = gs_modal_get_mode_curr();
  Mapping* mode_map = &mode->map;
  Command_Binding binding = map_get_binding_recursive(mode_map, map_id, event);
  
  // TODO(allen): map_id <-> map name?
  result.map = 0;
  result.command = binding.custom;
  
  return(result);
}

CUSTOM_COMMAND_SIG(gs_view_input_handler)
CUSTOM_DOC("Input consumption loop for default view behavior")
{
  Scratch_Block scratch(app);
  gs_input_handler_init(app, scratch);
  
  View_ID view = get_this_ctx_view(app, Access_Always);
  Managed_Scope scope = view_get_managed_scope(app, view);
  
  for (;;){
    // NOTE(allen): Get input
    User_Input input = get_next_input(app, EventPropertyGroup_Any, 0);
    if (input.abort){
      break;
    }

    
    
    ProfileScopeNamed(app, "before view input", view_input_profile);
    
    // NOTE(allen): Mouse Suppression
    Event_Property event_properties = get_event_properties(&input.event);
    if (suppressing_mouse && (event_properties & EventPropertyGroup_AnyMouseEvent) != 0){
      continue;
    }
    
    // NOTE(allen): Get binding
    if (implicit_map_function == 0){
      implicit_map_function = default_implicit_map;
    }
    Implicit_Map_Result map_result = implicit_map_function(app, 0, 0, &input.event);
    if (map_result.command == 0){
      leave_current_input_unhandled(app);
      continue;
    }
    
    // NOTE(allen): Run the command and pre/post command stuff
    default_pre_command(app, scope);
    ProfileCloseNow(view_input_profile);
    map_result.command(app);
    ProfileScope(app, "after view input");
    default_post_command(app, scope);
  }
}

BUFFER_HOOK_SIG(gs_begin_buffer){
  ProfileScope(app, "begin buffer");
  
  Scratch_Block scratch(app);
  
  b32 treat_as_code = false;
  String_Const_u8 file_name = push_buffer_file_name(app, scratch, buffer_id);
  if (file_name.size > 0){
    String_Const_u8 treat_as_code_string = def_get_config_string(scratch, vars_save_string_lit("treat_as_code"));
    String_Const_u8_Array extensions = parse_extension_line_to_extension_list(app, scratch, treat_as_code_string);
    String_Const_u8 ext = string_file_extension(file_name);
    for (i32 i = 0; i < extensions.count; ++i){
      if (string_match(ext, extensions.strings[i])){
        treat_as_code = true;
        break;
      }
    }
    
    if (string_match(file_name, S8Lit("*collections*")))
    {
      treat_as_code = true;
    }
  }
  
  String_ID file_map_id = vars_save_string_lit("keys_file");
  String_ID code_map_id = vars_save_string_lit("keys_code");
  
  Command_Map_ID map_id = (treat_as_code)?(code_map_id):(file_map_id);
  Managed_Scope scope = buffer_get_managed_scope(app, buffer_id);
  Command_Map_ID *map_id_ptr = scope_attachment(app, scope, buffer_map_id, Command_Map_ID);
  *map_id_ptr = map_id;
  
  Line_Ending_Kind setting = guess_line_ending_kind_from_buffer(app, buffer_id);
  Line_Ending_Kind *eol_setting = scope_attachment(app, scope, buffer_eol_setting, Line_Ending_Kind);
  *eol_setting = setting;
  
  // NOTE(allen): Decide buffer settings
  b32 wrap_lines = true;
  b32 use_lexer = false;
  if (treat_as_code){
    wrap_lines = def_get_config_b32(vars_save_string_lit("enable_code_wrapping"));
    use_lexer = true;
  }
  
  String_Const_u8 buffer_name = push_buffer_base_name(app, scratch, buffer_id);
  if (buffer_name.size > 0 && buffer_name.str[0] == '*' && buffer_name.str[buffer_name.size - 1] == '*'){
    wrap_lines = def_get_config_b32(vars_save_string_lit("enable_output_wrapping"));
  }
  
  if (use_lexer){
    ProfileBlock(app, "begin buffer kick off lexer");
    Async_Task *lex_task_ptr = scope_attachment(app, scope, buffer_lex_task, Async_Task);
    *lex_task_ptr = async_task_no_dep(&global_async_system, do_full_lex_async, make_data_struct(&buffer_id));
  }
  
  {
    b32 *wrap_lines_ptr = scope_attachment(app, scope, buffer_wrap_lines, b32);
    *wrap_lines_ptr = wrap_lines;
  }
  
  if (use_lexer){
    buffer_set_layout(app, buffer_id, layout_virt_indent_index_generic);
  }
  else{
    if (treat_as_code){
      buffer_set_layout(app, buffer_id, layout_virt_indent_literal_generic);
    }
    else{
      buffer_set_layout(app, buffer_id, layout_generic);
    }
  }
  
  // no meaning for return
  return(0);
}

BUFFER_EDIT_RANGE_SIG(gs_buffer_edit_range){
  // buffer_id, new_range, original_size
  ProfileScope(app, "default edit range");
  
  Range_i64 old_range = Ii64(old_cursor_range.min.pos, old_cursor_range.max.pos);
  
  buffer_shift_fade_ranges(buffer_id, old_range.max, (new_range.max - old_range.max));
  
  {
    code_index_lock();
    Code_Index_File *file = code_index_get_file(buffer_id);
    if (file != 0){
      code_index_shift(file, old_range, range_size(new_range));
    }
    code_index_unlock();
  }
  
  i64 insert_size = range_size(new_range);
  i64 text_shift = replace_range_shift(old_range, insert_size);
  
  Scratch_Block scratch(app);
  
  Managed_Scope scope = buffer_get_managed_scope(app, buffer_id);
  Async_Task *lex_task_ptr = scope_attachment(app, scope, buffer_lex_task, Async_Task);
  
  Base_Allocator *allocator = managed_scope_allocator(app, scope);
  b32 do_full_relex = false;
  
  if (async_task_is_running_or_pending(&global_async_system, *lex_task_ptr)){
    async_task_cancel(app, &global_async_system, *lex_task_ptr);
    buffer_unmark_as_modified(buffer_id);
    do_full_relex = true;
    *lex_task_ptr = 0;
  }
  
  Token_Array *ptr = scope_attachment(app, scope, attachment_tokens, Token_Array);
  if (ptr != 0 && ptr->tokens != 0){
    ProfileBlockNamed(app, "attempt resync", profile_attempt_resync);
    
    i64 token_index_first = token_relex_first(ptr, old_range.first, 1);
    i64 token_index_resync_guess =
      token_relex_resync(ptr, old_range.one_past_last, 16);
    
    if (token_index_resync_guess - token_index_first >= 4000){
      do_full_relex = true;
    }
    else{
      Token *token_first = ptr->tokens + token_index_first;
      Token *token_resync = ptr->tokens + token_index_resync_guess;
      
      Range_i64 relex_range = Ii64(token_first->pos, token_resync->pos + token_resync->size + text_shift);
      String_Const_u8 partial_text = push_buffer_range(app, scratch, buffer_id, relex_range);
      
      Token_List relex_list = lex_full_input_cpp(scratch, partial_text);
      if (relex_range.one_past_last < buffer_get_size(app, buffer_id)){
        token_drop_eof(&relex_list);
      }
      
      Token_Relex relex = token_relex(relex_list, relex_range.first - text_shift, ptr->tokens, token_index_first, token_index_resync_guess);
      
      ProfileCloseNow(profile_attempt_resync);
      
      if (!relex.successful_resync){
        do_full_relex = true;
      }
      else{
        ProfileBlock(app, "apply resync");
        
        i64 token_index_resync = relex.first_resync_index;
        
        Range_i64 head = Ii64(0, token_index_first);
        Range_i64 replaced = Ii64(token_index_first, token_index_resync);
        Range_i64 tail = Ii64(token_index_resync, ptr->count);
        i64 resynced_count = (token_index_resync_guess + 1) - token_index_resync;
        i64 relexed_count = relex_list.total_count - resynced_count;
        i64 tail_shift = relexed_count - (token_index_resync - token_index_first);
        
        i64 new_tokens_count = ptr->count + tail_shift;
        Token *new_tokens = base_array(allocator, Token, new_tokens_count);
        
        Token *old_tokens = ptr->tokens;
        block_copy_array_shift(new_tokens, old_tokens, head, 0);
        token_fill_memory_from_list(new_tokens + replaced.first, &relex_list, relexed_count);
        for (i64 i = 0, index = replaced.first; i < relexed_count; i += 1, index += 1){
          new_tokens[index].pos += relex_range.first;
        }
        for (i64 i = tail.first; i < tail.one_past_last; i += 1){
          old_tokens[i].pos += text_shift;
        }
        block_copy_array_shift(new_tokens, ptr->tokens, tail, tail_shift);
        
        base_free(allocator, ptr->tokens);
        
        ptr->tokens = new_tokens;
        ptr->count = new_tokens_count;
        ptr->max = new_tokens_count;
        
        buffer_mark_as_modified(buffer_id);
      }
    }
  }
  
  if (do_full_relex){
    *lex_task_ptr = async_task_no_dep(&global_async_system, do_full_lex_async,
                                      make_data_struct(&buffer_id));
  }
  
  gs_collection_on_buffer_edit(app, buffer_id, old_range, new_range);
  
  // no meaning for return
  return(0);
}


f32 time_since_last_dirty_buffers_check = 0;

function void
reload_clean_buffers_on_filesystem_change(Application_Links* app, Frame_Info frame_info)
{
  time_since_last_dirty_buffers_check += frame_info.literal_dt;
  if (time_since_last_dirty_buffers_check > 1) {
    time_since_last_dirty_buffers_check = 0;
    for (Buffer_ID buffer = get_buffer_next(app, 0, Access_Always);
      buffer != 0;
      buffer = get_buffer_next(app, buffer, Access_Always)){
      Dirty_State dirty = buffer_get_dirty_state(app, buffer);
      if (dirty == DirtyState_UnloadedChanges) {
        buffer_reopen(app, buffer, 0);
      }
    }
  }
}


function void
gs_tick(Application_Links *app, Frame_Info frame_info){
    code_index_update_tick(app);
    
    ////////////////////////////////
    // NOTE(allen): Update fade ranges
    if (tick_all_fade_ranges(app, frame_info.animation_dt)){
        animate_in_n_milliseconds(app, 0);
    }
    
    ////////////////////////////////
    // NOTE(allen): Clear layouts if virtual whitespace setting changed.
    b32 enable_virtual_whitespace = def_get_config_b32(vars_save_string_lit("enable_virtual_whitespace"));
    if (enable_virtual_whitespace != def_enable_virtual_whitespace){
        def_enable_virtual_whitespace = enable_virtual_whitespace;
        clear_all_layouts(app);
    }

    ////////////////////////////////
    // NOTE(PS): reload clean buffers with external edits
    reload_clean_buffers_on_filesystem_change(app, frame_info);
}

function void
set_all_gs_hooks(Application_Links* app)
{
  set_custom_hook(app, HookID_BufferViewerUpdate, default_view_adjust);
  
  set_custom_hook(app, HookID_ViewEventHandler, gs_view_input_handler);
  set_custom_hook(app, HookID_Tick, gs_tick);
  set_custom_hook(app, HookID_RenderCaller, gs_render_caller);
  set_custom_hook(app, HookID_WholeScreenRenderCaller, default_whole_screen_render_caller);
  
  set_custom_hook(app, HookID_DeltaRule, fixed_time_cubic_delta);
  set_custom_hook_memory_size(app, HookID_DeltaRule,
                              delta_ctx_size(fixed_time_cubic_delta_memory_size));
  
  set_custom_hook(app, HookID_BufferNameResolver, default_buffer_name_resolution);
  
  set_custom_hook(app, HookID_BeginBuffer, gs_begin_buffer);
  set_custom_hook(app, HookID_EndBuffer, end_buffer_close_jump_list);
  set_custom_hook(app, HookID_NewFile, default_new_file);
  set_custom_hook(app, HookID_SaveFile, default_file_save);
  set_custom_hook(app, HookID_BufferEditRange, gs_buffer_edit_range);
  set_custom_hook(app, HookID_BufferRegion, default_buffer_region);
  set_custom_hook(app, HookID_ViewChangeBuffer, default_view_change_buffer);
  
  set_custom_hook(app, HookID_Layout, layout_unwrapped);
  //set_custom_hook(app, HookID_Layout, layout_wrap_anywhere);
  //set_custom_hook(app, HookID_Layout, layout_wrap_whitespace);
  //set_custom_hook(app, HookID_Layout, layout_virt_indent_unwrapped);
  //set_custom_hook(app, HookID_Layout, layout_unwrapped_small_blank_lines);
  
  implicit_map_function = gs_implicit_map;
}