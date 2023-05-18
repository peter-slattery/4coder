CUSTOM_COMMAND_SIG(gs_startup)
CUSTOM_DOC("command for responding to a startup event")
{
  ProfileScope(app, "gs startup");
  User_Input input = get_current_input(app);
  if (match_core_code(&input, CoreCode_Startup)){
    String_Const_u8_Array file_names = input.event.core.file_names;
    load_themes_default_folder(app);
    default_4coder_initialize(app, file_names);
    
    // Setup default layout
    
    Buffer_ID comp_buffer = create_buffer(
      app, 
      S8Lit("*compilation*"), 
      BufferCreate_NeverAttachToFile | BufferCreate_AlwaysNew
    );
    buffer_set_setting(app, comp_buffer, BufferSetting_Unimportant, true);
    buffer_set_setting(app, comp_buffer, BufferSetting_ReadOnly, true);
    
    Buffer_Identifier comp_name = buffer_identifier(S8Lit("*compilation*"));
    Buffer_Identifier code_left_name = buffer_identifier(S8Lit("*scratch*"));
    Buffer_Identifier code_right_name = buffer_identifier(S8Lit("*messages*"));
    
    Buffer_ID comp_id = buffer_identifier_to_id(app, comp_name);
    Buffer_ID code_left_id = buffer_identifier_to_id(app, code_left_name);
    Buffer_ID code_right_id = buffer_identifier_to_id(app, code_right_name);
    
    // Left Panel
    View_ID left_view = get_active_view(app, Access_Always);
    new_view_settings(app, left_view);
    view_set_buffer(app, left_view, code_left_id, 0);
    
    // NOTE(rjf): Bottom panel
    View_ID compilation_view = 0;
    {
      compilation_view = open_view(app, left_view, ViewSplit_Bottom);
      new_view_settings(app, compilation_view);
      Buffer_ID buffer = view_get_buffer(app, compilation_view, Access_Always);
      Face_ID face_id = get_face_id(app, buffer);
      Face_Metrics metrics = get_face_metrics(app, face_id);
      view_set_split_pixel_size(app, compilation_view, (i32)(metrics.line_height*4.f));
      view_set_passive(app, compilation_view, true);
      global_compilation_view = compilation_view;
      view_set_buffer(app, compilation_view, comp_id, 0);
    }
    
    // Right Panel
    view_set_active(app, left_view);
    open_panel_vsplit(app);
    View_ID right_view = get_active_view(app, Access_Always);
    view_set_buffer(app, right_view, code_right_id, 0);
    
    view_set_active(app, left_view);
  }
  
  {
    //def_audio_init();
  }
  
  {
    def_enable_virtual_whitespace = def_get_config_b32(vars_save_string_lit("enable_virtual_whitespace"));
    clear_all_layouts(app);
  }
  
  system_set_fullscreen(false);
}


CUSTOM_COMMAND_SIG(gs_delete_to_end_of_line)
CUSTOM_DOC("Deletes all text from the cursor to the end of the line")
{
  View_ID view = get_active_view(app, Access_ReadWriteVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  i64 pos = view_get_cursor_pos(app, view);
  i64 line_number = get_line_number_from_pos(app, buffer, pos);
  Range_Cursor line_range = get_line_range(app, buffer, line_number);
  Range_i64 delete_range = {};
  
  if (line_range.start.line != 0 && line_range.end.line != 0)
  {
    delete_range = Ii64(pos, line_range.end.pos);
  }
  
  if (range_size(delete_range) == 0)
  {
    delete_range.end += 1;
    i32 buffer_size = (i32)buffer_get_size(app, buffer);
    delete_range.end = clamp_top(delete_range.end, buffer_size);
  }
  
  buffer_replace_range(app, buffer, delete_range, string_u8_litexpr(""));
}

function i64
F4_Boundary_TokenAndWhitespace(Application_Links *app, Buffer_ID buffer, 
  Side side, Scan_Direction direction, i64 pos)
{
  i64 result = boundary_non_whitespace(app, buffer, side, direction, pos);
  Token_Array tokens = get_token_array_from_buffer(app, buffer);
  if (tokens.tokens != 0){
    switch (direction){
      case Scan_Forward:
      {
        i64 buffer_size = buffer_get_size(app, buffer);
        result = buffer_size;
        if(tokens.count > 0)
        {
          Token_Iterator_Array it = token_iterator_pos(0, &tokens, pos);
          Token *token = token_it_read(&it);
          
          if(token == 0)
          {
            break;
          }
          
          // NOTE(rjf): Comments/Strings
          if(token->kind == TokenBaseKind_Comment ||
              token->kind == TokenBaseKind_LiteralString)
          {
            result = boundary_non_whitespace(app, buffer, side, direction, pos);
            break;
          }
          
          // NOTE(rjf): All other cases.
          else
          {
            if (token->kind == TokenBaseKind_Whitespace)
            {
              // token_it_inc_non_whitespace(&it);
              // token = token_it_read(&it);
            }
            
            if (side == Side_Max){
              result = token->pos + token->size;
              
              token_it_inc_all(&it);
              Token *ws = token_it_read(&it);
              if(ws != 0 && ws->kind == TokenBaseKind_Whitespace &&
                  get_line_number_from_pos(app, buffer, ws->pos + ws->size) ==
                  get_line_number_from_pos(app, buffer, token->pos))
              {
                result = ws->pos + ws->size;
              }
            }
            else{
              if (token->pos <= pos){
                token_it_inc_non_whitespace(&it);
                token = token_it_read(&it);
              }
              if (token != 0){
                result = token->pos;
              }
            }
          }
          
        }
      }break;
      
      case Scan_Backward:
      {
        result = 0;
        if (tokens.count > 0){
          Token_Iterator_Array it = token_iterator_pos(0, &tokens, pos);
          Token *token = token_it_read(&it);
          
          Token_Iterator_Array it2 = it;
          token_it_dec_non_whitespace(&it2);
          Token *token2 = token_it_read(&it2);
          
          // NOTE(rjf): Comments/Strings
          if(token->kind == TokenBaseKind_Comment ||
              token->kind == TokenBaseKind_LiteralString ||
            (token2 && 
                token2->kind == TokenBaseKind_Comment ||
                token2->kind == TokenBaseKind_LiteralString))
          {
            result = boundary_non_whitespace(app, buffer, side, direction, pos);
            break;
          }
          
          if (token->kind == TokenBaseKind_Whitespace){
            token_it_dec_non_whitespace(&it);
            token = token_it_read(&it);
          }
          if (token != 0){
            if (side == Side_Min){
              if (token->pos >= pos){
                token_it_dec_non_whitespace(&it);
                token = token_it_read(&it);
              }
              result = token->pos;
            }
            else{
              if (token->pos + token->size >= pos){
                token_it_dec_non_whitespace(&it);
                token = token_it_read(&it);
              }
              result = token->pos + token->size;
            }
          }
        }
      }break;
    }
  }
  return(result);
}

CUSTOM_COMMAND_SIG(gs_backspace_char)
CUSTOM_DOC("Deletes the last character, or, if only whitespace on the line, to the beginning of the line")
{
  View_ID view = get_active_view(app, Access_ReadWriteVisible);
  if (!if_view_has_highlighted_range_delete_range(app, view)){
    Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
    i64 end = view_get_cursor_pos(app, view);
    i64 buffer_size = buffer_get_size(app, buffer);
    if (0 < end && end <= buffer_size){
      // Gets the last character
      Buffer_Cursor cursor = view_compute_cursor(app, view, seek_pos(end));
      i64 character = view_relative_character_from_pos(app, view, cursor.line, cursor.pos);
      i64 start = view_pos_from_relative_character(app, view, cursor.line, character - 1);
      
      // If the character is whitespace, continu
      i64 start_initial = start;
      u8 char_deleting = buffer_get_char(app, buffer, start);
      if (char_deleting == ' ') 
      {
        while (char_deleting == ' ' && start > 0) 
        {
          start -= 1;
          char_deleting = buffer_get_char(app, buffer, start);
        }
        // If we iterated back thru whitespace and found something other than
        // the beginning of the line, undo the iteration and just
        // delete a single character
        bool is_at_line_start = char_deleting == '\n' || char_deleting == '\r';
        bool is_at_buffer_start = start == 0;
        if (is_at_line_start) 
        {
          start += 1; // step forward from the newline
        }
        else if (!is_at_buffer_start) 
        {
          start = start_initial;
        }
      }
      
      if (buffer_replace_range(app, buffer, Ii64(start, end), string_u8_empty)) 
      {
        view_set_cursor_and_preferred_x(app, view, seek_pos(start));
      }
    }
  }
}

CUSTOM_COMMAND_SIG(gs_backspace_alpha_numeric_or_camel_boundary)
CUSTOM_DOC("Deletes left to a alphanumeric or camel boundary.")
{
  Scratch_Block scratch(app);
  current_view_boundary_delete(app, Scan_Backward, push_boundary_list(scratch,
    boundary_line,
    boundary_alpha_numeric,
    boundary_alpha_numeric_camel));
}

CUSTOM_COMMAND_SIG(gs_backspace_token_boundary)
CUSTOM_DOC("Deletes left to a token boundary.")
{
  Scratch_Block scratch(app);
  Boundary_Function_List boundary_list = push_boundary_list(scratch, F4_Boundary_TokenAndWhitespace);
  current_view_boundary_delete(app, Scan_Backward, boundary_list);
}

function i64
gs_get_line_indent_level(Application_Links *app, View_ID view, Buffer_ID buffer, i64 line)
{
  Scratch_Block scratch(app);
  
  String_Const_u8 line_string = push_buffer_line(app, scratch, buffer, line);
  i64 line_start_pos = get_line_start_pos(app, buffer, line);
  
  Range_i64 line_indent_range = Ii64(0, 0);
  i64 tabs_at_beginning = 0;
  i64 spaces_at_beginning = 0;
  for(u64 i = 0; i < line_string.size; i += 1)
  {
    if(line_string.str[i] == '\t')
    {
      tabs_at_beginning += 1;
    }
    else if(character_is_whitespace(line_string.str[i]))
    {
      spaces_at_beginning += 1;
    }
    else if(!character_is_whitespace(line_string.str[i]))
    {
      line_indent_range.max = (i64)i;
      break;
    }
  }
  
  // NOTE(PS): This is in the event that we are unindenting a line that
  // is JUST tabs or spaces - rather than unindenting nothing
  // and then reindenting the proper amount, this should cause
  // the removal of all leading tabs and spaces on an otherwise
  // empty line
  bool place_cursor_at_end = false;
  if (line_indent_range.max == 0 && line_string.size > 0)
  {
    line_indent_range.max = line_string.size;
    place_cursor_at_end = true;
  }
  
  Range_i64 indent_range =
  {
    line_indent_range.min + line_start_pos,
    line_indent_range.max + line_start_pos,
  };
  
  i64 indent_width = (i64)def_get_config_u64(app, vars_save_string_lit("indent_width"));
  i64 spaces_per_indent_level = indent_width;
  i64 indent_level = spaces_at_beginning / spaces_per_indent_level + tabs_at_beginning;
  
  return indent_level;
}

CUSTOM_COMMAND_SIG(gs_write_text_and_auto_indent)
CUSTOM_DOC("Inserts text and auto-indents the line on which the cursor sits if any of the text contains 'layout punctuation' such as ;:{}()[]# and new lines.")
{
  ProfileScope(app, "write and auto indent");
  
  Scratch_Block scratch(app);
  
  User_Input in = get_current_input(app);
  String_Const_u8 insert = to_writable(&in);
  if (insert.str != 0 && insert.size > 0){
    b32 do_auto_indent = false;
    b32 only_indent_next_line = true;
    b32 is_newline = false;
    for (u64 i = 0; !do_auto_indent && i < insert.size; i += 1){
      switch (insert.str[i]){
        case ';': case ':':
        case '{': case '}':
        case '(': case ')':
        case '[': case ']':
        case '#':
        {
          do_auto_indent = true;
        }break;
        
        case '\n': case '\t':
        {
          do_auto_indent = true;
          is_newline = true;
        }break;
      }
    }
    
    View_ID view = get_active_view(app, Access_ReadWriteVisible);
    Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
    
    String_Const_u8 file_name = push_buffer_file_name(app, scratch, buffer);
    String_Const_u8 ext = string_file_extension(file_name);
    if (string_match(ext, string_u8_litexpr("js")) ||
        string_match(ext, string_u8_litexpr("css")))
    {
      only_indent_next_line = do_auto_indent;
    }
    
    if (do_auto_indent){
      Range_i64 pos = {};
      if (view_has_highlighted_range(app, view)){
        pos = get_view_range(app, view);
      }
      else{
        pos.min = pos.max = view_get_cursor_pos(app, view);
      }
      
      write_text_input(app);
      
      i64 end_pos = view_get_cursor_pos(app, view);
      if (!only_indent_next_line)
      {
        pos.min = Min(pos.min, end_pos);
        pos.max = Max(pos.max, end_pos);
        auto_indent_buffer(app, buffer, pos, 0);
        move_past_lead_whitespace(app, view, buffer);
      } else if (only_indent_next_line && is_newline) {
        
        // indent the new line to the same level
        // as the line the cursor was just on
        i64 indent_width = (i64)def_get_config_u64(app, vars_save_string_lit("indent_width"));
        b32 indent_with_tabs = def_get_config_b32(vars_save_string_lit("indent_with_tabs"));
        
        String_Const_u8 indent_string = indent_with_tabs ? string_u8_litexpr("\t") : push_stringf(scratch, "%.*s", Min(indent_width, 16),
          "                ");
        
        // getting the indent from the PREVIOUS line, not the line
        // the cursor is about to be on - since that line is new,
        // and therefore, empty
        i64 line = get_line_number_from_pos(app, buffer, pos.min);
        i64 indent_level = gs_get_line_indent_level(app, view, buffer, line);
        
        pos.min = pos.max = end_pos;
        
        for(i64 i = 0; i < indent_level; i += 1)
        {
          buffer_replace_range(app, buffer, Ii64(pos.max), indent_string);
        }
        
        move_past_lead_whitespace(app, view, buffer);
      }
      
      
    }
    else{
      write_text_input(app);
    }
  }
}

CUSTOM_COMMAND_SIG(gs_input_enter_behavior)
{
  View_ID view = get_active_view(app, Access_ReadVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  if (buffer == 0)
  {
    buffer = view_get_buffer(app, view, Access_ReadVisible);
    if (buffer != 0)
    {
      goto_jump_at_cursor(app);
      lock_jump_buffer(app, buffer);
    }
  }
  else
  {
    
    leave_current_input_unhandled(app);
  }
}

CUSTOM_COMMAND_SIG(input_alt_enter_behavior)
{
  View_ID view = get_active_view(app, Access_ReadVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  if (buffer == 0){
    buffer = view_get_buffer(app, view, Access_ReadVisible);
    if (buffer != 0){
      goto_jump_at_cursor_same_panel(app);
      lock_jump_buffer(app, buffer);
    }
  }
  else
  {
    leave_current_input_unhandled(app);
  }
}

function String_Const_u8
get_lexeme_under_cursor(Application_Links* app, View_ID view, Buffer_ID buffer, Arena* arena)
{
  String_Const_u8 lexeme = {0};
  i64 pos = view_get_cursor_pos(app, view);
  Token* token = get_token_from_pos(app, buffer, pos);
  if (token != 0) {
    lexeme = push_token_lexeme(app, arena, buffer, token);
  }
  return lexeme;
}

static String_Const_u8 last_lexeme = {};
static u64             last_lexeme_index = 0;

function void
go_to_definition(Application_Links* app, String_Const_u8 lexeme, View_ID view)
{
  Code_Index_Note* note = 0;
  
  // if we're trying to go to the definition of the same lexeme as last time
  // then there are probably a typedef + declaration in different locations so
  // we want to advance to the next code index note that matches this lexeme
  // and then loop
  if (string_match(last_lexeme, lexeme))
  {
    Code_Index_Note_List* list = code_index__list_from_string(lexeme);
    u64 i = 0;
    for (Code_Index_Note *it = list->first;
      it != 0;
      it = it->next_in_hash, i++){
      if (string_match(lexeme, it->text) && i > last_lexeme_index){
        note = it;
        last_lexeme_index = i;
        break;
      }
    }
  }
  
  if (!note)
  {
    note = code_index_note_from_string(lexeme);
    last_lexeme = lexeme;
    last_lexeme_index = 0;
  }
  if (note == 0) return;
  
  Buffer_ID buffer = note->file->buffer;
  view_set_buffer(app, view, buffer, 0);
  
  switch (note->note_kind)
  {
    case CodeIndexNote_Type:
    case CodeIndexNote_Function:
    case CodeIndexNote_Macro:
    {
      jump_to_location(app, view, note->file->buffer, note->pos.start);
    } break;
    
    default: {} break;
  }
}

CUSTOM_COMMAND_SIG(cmd_enter_behavior)
{
  View_ID view = get_active_view(app, Access_ReadVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  if (buffer == 0){
    buffer = view_get_buffer(app, view, Access_ReadVisible);
    if (buffer != 0){
      goto_jump_at_cursor(app);
      lock_jump_buffer(app, buffer);
    }
  }
  else{
    Scratch_Block scratch(app);
    String_Const_u8 lexeme = get_lexeme_under_cursor(app, view, buffer, scratch);
    if (lexeme.size > 0) {
      go_to_definition(app, lexeme, view);
    }
  }
}

CUSTOM_COMMAND_SIG(cmd_alt_enter_behavior)
{
  View_ID view = get_active_view(app, Access_ReadVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  if (buffer == 0){
    buffer = view_get_buffer(app, view, Access_ReadVisible);
    if (buffer != 0){
      goto_jump_at_cursor_same_panel(app);
      lock_jump_buffer(app, buffer);
    }
  }
  else{
    Scratch_Block scratch(app);
    String_Const_u8 lexeme = get_lexeme_under_cursor(app, view, buffer, scratch);
    if (lexeme.size > 0) {
      view = get_next_view_looped_primary_panels(app, view, Access_Always);
      go_to_definition(app, lexeme, view);
    }
  }
}

CUSTOM_COMMAND_SIG(toggle_compilation_view)
{
  Buffer_ID buffer = view_get_buffer(app, global_compilation_view, Access_Always);
  Face_ID face_id = get_face_id(app, buffer);
  Face_Metrics metrics = get_face_metrics(app, face_id);
  if(global_compilation_view_expanded ^= 1)
  {
    view_set_split_pixel_size(app, global_compilation_view, (i32)(metrics.line_height*32.f));
  }
  else
  {
    view_set_split_pixel_size(app, global_compilation_view, (i32)(metrics.line_height*4.f));
  }
}

CUSTOM_COMMAND_SIG(gs_indent_or_autocomplete)
{
  Scratch_Block scratch(app);
  
  View_ID view = get_active_view(app, Access_ReadVisible);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  if (buffer != 0)
  {
    i64 pos = view_get_cursor_pos(app, view);
    Buffer_Cursor buffer_cursor = buffer_compute_cursor(app, buffer, seek_pos(pos));
    Buffer_Cursor line_start_cursor = get_line_start(app, buffer, buffer_cursor.line);
    u8 char_before = buffer_get_char(app, buffer, pos - 1);
    if ((buffer_cursor.pos == line_start_cursor.pos) || character_is_whitespace(char_before))
    {
      i64 indent_width = (i64)def_get_config_u64(app, vars_save_string_lit("indent_width"));
      b32 indent_with_tabs = def_get_config_b32(vars_save_string_lit("indent_with_tabs"));
      String_Const_u8 indent_string = indent_with_tabs ? S8Lit("\t") : push_stringf(scratch, "%.*s", Min(indent_width, 16),
        "                ");
      write_text(app, indent_string);
    }
    else
    {
      word_complete(app);
    }
  }
}

function void
F4_ReIndentLine(Application_Links *app, Buffer_ID buffer, i64 line, i64 indent_delta)
{
  Scratch_Block scratch(app);
  View_ID view = get_active_view(app, Access_ReadWriteVisible);
  
  String_Const_u8 line_string = push_buffer_line(app, scratch, buffer, line);
  i64 line_start_pos = get_line_start_pos(app, buffer, line);
  
  Range_i64 line_indent_range = Ii64(0, 0);
  i64 tabs_at_beginning = 0;
  i64 spaces_at_beginning = 0;
  for(u64 i = 0; i < line_string.size; i += 1)
  {
    if(line_string.str[i] == '\t')
    {
      tabs_at_beginning += 1;
    }
    else if(character_is_whitespace(line_string.str[i]))
    {
      spaces_at_beginning += 1;
    }
    else if(!character_is_whitespace(line_string.str[i]))
    {
      line_indent_range.max = (i64)i;
      break;
    }
  }
  
  // NOTE(PS): This is in the event that we are unindenting a line that
  // is JUST tabs or spaces - rather than unindenting nothing
  // and then reindenting the proper amount, this should cause
  // the removal of all leading tabs and spaces on an otherwise
  // empty line
  bool place_cursor_at_end = false;
  if (line_indent_range.max == 0 && line_string.size == (u64)(spaces_at_beginning + tabs_at_beginning))
  {
    line_indent_range.max = line_string.size;
    place_cursor_at_end = true;
  }
  
  // NOTE(rjf): Indent lines.
  {
    Range_i64 indent_range =
    {
      line_indent_range.min + line_start_pos,
      line_indent_range.max + line_start_pos,
    };
    
    i64 indent_width = (i64)def_get_config_u64(app, vars_save_string_lit("indent_width"));
    b32 indent_with_tabs = def_get_config_b32(vars_save_string_lit("indent_with_tabs"));
    i64 spaces_per_indent_level = indent_width;
    i64 indent_level = spaces_at_beginning / spaces_per_indent_level + tabs_at_beginning;
    i64 new_indent_level = indent_level + indent_delta;
    
    String_Const_u8 indent_string = indent_with_tabs ? S8Lit("\t") : push_stringf(scratch, "%.*s", Min(indent_width, 16),
      "                ");
    buffer_replace_range(app, buffer, indent_range, S8Lit(""));
    for(i64 i = 0; i < new_indent_level; i += 1)
    {
      buffer_replace_range(app, buffer, Ii64(line_start_pos), indent_string);
    }
    
    if (place_cursor_at_end)
    {
      // update line_string now that we've edited the line
      line_string = push_buffer_line(app, scratch, buffer, line);
      
      line_start_pos = get_line_start_pos(app, buffer, line);
      i64 line_end_pos = line_start_pos + line_string.size;
      view_set_cursor(app, view, seek_pos(line_end_pos));
    }
  }
  
}

internal void
F4_ReIndentLineRange(Application_Links *app, Buffer_ID buffer, Range_i64 range, i64 indent_delta)
{
  for(i64 i = range.min; i <= range.max; i += 1)
  {
    F4_ReIndentLine(app, buffer, i, indent_delta);
  }
}

internal Range_i64
F4_LineRangeFromPosRange(Application_Links *app, Buffer_ID buffer, Range_i64 pos_range)
{
  Range_i64 lines_range =
    Ii64(get_line_number_from_pos(app, buffer, pos_range.min),
    get_line_number_from_pos(app, buffer, pos_range.max));
  return lines_range;
}

internal Range_i64
F4_PosRangeFromLineRange(Application_Links *app, Buffer_ID buffer, Range_i64 line_range)
{
  if(line_range.min > line_range.max)
  {
    i64 swap = line_range.max;
    line_range.max = line_range.min;
    line_range.min = swap;
  }
  Range_i64 pos_range =
    Ii64(get_line_start_pos(app, buffer, line_range.min),
    get_line_end_pos(app, buffer, line_range.max));
  return pos_range;
}

internal void
F4_ReIndentPosRange(Application_Links *app, Buffer_ID buffer, Range_i64 range, i64 indent_delta)
{
  F4_ReIndentLineRange(app, buffer,
    F4_LineRangeFromPosRange(app, buffer, range),
    indent_delta);
}

CUSTOM_COMMAND_SIG(gs_unindent_line)
{
  View_ID view = get_active_view(app, Access_ReadWrite);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWriteVisible);
  i64 pos = view_get_cursor_pos(app, view);
  i64 line = get_line_number_from_pos(app, buffer, pos);
  F4_ReIndentLine(app, buffer, line, -1);
}

internal void
F4_AdjustCursorAndMarkForIndentation(Application_Links *app, View_ID view, i64 original_cursor, i64 original_mark, Range_i64 original_line_range)
{
  Buffer_ID buffer = view_get_buffer(app, view, Access_Read);
  Scratch_Block scratch(app);
  if(original_cursor == original_mark)
  {
    i64 start_pos = get_line_start_pos(app, buffer, original_line_range.min);
    i64 new_pos = start_pos;
    String_Const_u8 line = push_buffer_line(app, scratch, buffer, original_line_range.min);
    for(u64 i = 0; i < line.size; i += 1)
    {
      if(!character_is_whitespace(line.str[i]))
      {
        new_pos = start_pos + (i64)i;
        break;
      }
    }
    
    view_set_cursor(app, view, seek_pos(new_pos));
    view_set_mark(app, view, seek_pos(new_pos));
  }
  else
  {
    Range_i64 range = F4_PosRangeFromLineRange(app, buffer, original_line_range);
    view_set_cursor(app, view, seek_pos(original_cursor > original_mark ? range.max : range.min));
    view_set_mark(app, view, seek_pos(original_cursor > original_mark ? range.min : range.max));
  }
}

function void
gs_update_range_indentation(Application_Links* app, i32 indent_offset)
{
  Scratch_Block scratch(app);
  View_ID view = get_active_view(app, Access_ReadWrite);
  Buffer_ID buffer = view_get_buffer(app, view, Access_ReadWrite);
  i64 pos = view_get_cursor_pos(app, view);
  i64 mark = view_get_mark_pos(app, view);
  Range_i64 pos_range = Ii64(pos, mark);
  Range_i64 line_range = F4_LineRangeFromPosRange(app, buffer, pos_range);
  History_Group group = history_group_begin(app, buffer);
  F4_ReIndentPosRange(app, buffer, Ii64(pos, mark), indent_offset);
  F4_AdjustCursorAndMarkForIndentation(app, view, pos, mark, line_range);
  history_group_end(group);
  no_mark_snap_to_cursor(app, view);
}

CUSTOM_COMMAND_SIG(gs_indent_range)
{
  gs_update_range_indentation(app, 1);
}

CUSTOM_COMMAND_SIG(gs_unindent_range)
{
  gs_update_range_indentation(app, -1);
}


// Only on mac, switch Alt and Command key codes
#define EXTERNAL_KEYBOARD 0
#if OS_MAC && !EXTERNAL_KEYBOARD
static u32 key_alt = KeyCode_Command;
#else
static u32 key_alt = KeyCode_Alt;
#endif

function void
gs_bindings_cmd_misc(Mapping* m, Command_Map* map)
{
  Bind(command_lister, KeyCode_W);
  Bind(change_active_panel, KeyCode_E);
  Bind(toggle_compilation_view, KeyCode_Minus);
}

function void
gs_bindings_cmd_file_ops(Mapping* m, Command_Map* map)
{
  Bind(set_mark, KeyCode_Space);
  Bind(interactive_open_or_new, KeyCode_Comma);
  Bind(interactive_switch_buffer, KeyCode_Period);
  Bind(save, KeyCode_Semicolon);
}

function void
gs_bindings_cmd_search(Mapping* m, Command_Map* map)
{
  Bind(query_replace, KeyCode_S);
  Bind(search, KeyCode_F);
  Bind(list_all_locations_of_identifier, KeyCode_D);
  Bind(list_all_substring_locations_case_insensitive, KeyCode_D, key_alt);
  Bind(goto_next_jump, KeyCode_T);
  Bind(goto_prev_jump, KeyCode_R);
  
  // Listers
  Bind(gs_lister_search_types, KeyCode_1);
  Bind(gs_lister_search_functions, KeyCode_2);
  Bind(gs_lister_search_macros, KeyCode_3);
  Bind(gs_lister_search_all, KeyCode_4);
  Bind(gs_search_collection, KeyCode_5);
}

function void
gs_bindings_cmd_nav(Mapping* m, Command_Map* map)
{
  Bind(seek_beginning_of_line, KeyCode_Y);
  Bind(seek_end_of_line, KeyCode_P);
  Bind(move_left_token_boundary, KeyCode_U);
  Bind(move_right_token_boundary, KeyCode_O);
  
  Bind(move_up, KeyCode_I);
  Bind(move_left, KeyCode_J);
  Bind(move_down, KeyCode_K);
  Bind(move_right, KeyCode_L);
  
  
  Bind(move_up_to_blank_line_end, KeyCode_H);
  Bind(move_down_to_blank_line_end, KeyCode_N);
  
  Bind(cmd_enter_behavior, KeyCode_Return);
  Bind(cmd_alt_enter_behavior, KeyCode_Return, key_alt);
  Bind(jump_to_last_point, KeyCode_Semicolon, KeyCode_Control);
}

function void
gs_bindings()
{
  gs_modal_set_cursor_color_u32(gs_modal_mode_input, 0xFF00FF00);
  gs_modal_set_cursor_color_u32(gs_modal_mode_cmd,   0xFFFF0000);
  gs_modal_set_cursor_color_u32(gs_modal_mode_debug, 0xFF00F0FF);
  
  MappingScope();
  
  // Global commands
  gs_modal_bind_all(gs_modal_map_id_global, gs_modal_set_mode_toggle, KeyCode_F, key_alt, 0);
  gs_modal_bind_all(gs_modal_map_id_global, gs_modal_set_mode_next, KeyCode_F, KeyCode_Control, 0);
  gs_modal_bind_all(gs_modal_map_id_global, exit_4coder, KeyCode_F4, key_alt, 0);
  
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F1, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F2, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F3, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F4, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F5, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F6, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F7, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F8, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F9, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F10, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F11, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F12, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F13, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F14, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F15, 0, 0);
  gs_modal_bind_all(gs_modal_map_id_global, project_fkey_command, KeyCode_F16, 0, 0);
  
  SelectMapping(&gs_modal_get_mode(gs_modal_mode_cmd)->map);
  SelectMap(gs_modal_map_id_global);
  {
    gs_bindings_cmd_file_ops(m, map);
    gs_bindings_cmd_misc(m, map);
    gs_bindings_cmd_search(m, map);
    gs_bindings_cmd_nav(m, map);
    
    // Text Editing
    Bind(gs_delete_to_end_of_line, KeyCode_A);
    Bind(undo, KeyCode_Z);
    Bind(redo, KeyCode_B);
    Bind(copy, KeyCode_C);
    Bind(paste, KeyCode_V);
    Bind(cut, KeyCode_X);
    Bind(gs_backspace_char, KeyCode_Backspace);
    Bind(gs_backspace_alpha_numeric_or_camel_boundary, KeyCode_Backspace, key_alt);
    Bind(gs_backspace_token_boundary, KeyCode_Backspace, KeyCode_Control);
    Bind(gs_unindent_range, KeyCode_Tab, KeyCode_Shift);
    Bind(gs_indent_range, KeyCode_Tab);
    
    // Macros
    Bind(keyboard_macro_start_recording, KeyCode_1, key_alt);
    Bind(keyboard_macro_finish_recording, KeyCode_2, key_alt);
    Bind(keyboard_macro_replay, KeyCode_3, key_alt);
  }
  
  SelectMapping(&gs_modal_get_mode(gs_modal_mode_input)->map);
  SelectMap(gs_modal_map_id_global);
  {
    BindMouse(click_set_cursor_and_mark, MouseCode_Left);
    BindMouseRelease(click_set_cursor, MouseCode_Left);
    BindCore(click_set_cursor_and_mark, CoreCode_ClickActivateView);
    BindMouseMove(click_set_cursor_if_lbutton);
    
    Bind(delete_char,            KeyCode_Delete);
    Bind(gs_backspace_char,      KeyCode_Backspace);
    Bind(gs_backspace_alpha_numeric_or_camel_boundary, KeyCode_Backspace, key_alt);
    Bind(gs_backspace_token_boundary, KeyCode_Backspace, KeyCode_Control);
    
    Bind(move_up,                KeyCode_I, key_alt);
    Bind(move_down,              KeyCode_K, key_alt);
    
    BindTextInput(gs_write_text_and_auto_indent);
    Bind(gs_indent_or_autocomplete, KeyCode_Tab);
    Bind(gs_unindent_line, KeyCode_Tab, KeyCode_Shift);
    Bind(write_todo,                 KeyCode_T, key_alt);
    Bind(write_note,                 KeyCode_G, key_alt);
    
    Bind(gs_input_enter_behavior, KeyCode_Return);
    Bind(input_alt_enter_behavior, KeyCode_Return, key_alt);
  }
  
  SelectMapping(&gs_modal_get_mode(gs_modal_mode_debug)->map);
  SelectMap(gs_modal_map_id_global);
  {
    gs_bindings_cmd_file_ops(m, map);
    gs_bindings_cmd_misc(m, map);
    gs_bindings_cmd_search(m, map);
    gs_bindings_cmd_nav(m, map);
  }
}