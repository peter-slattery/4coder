internal void
gs_print_string_match_list_to_buffer(Application_Links *app, Buffer_ID out_buffer_id, String_Match_List matches){
  Scratch_Block scratch(app);
  
  Variable_Handle prj_var = vars_read_key(vars_get_root(), vars_save_string_lit("prj_config"));
  String8 prj_dir = prj_path_from_project(scratch, prj_var);
  
  clear_buffer(app, out_buffer_id);
  Buffer_Insertion out = begin_buffer_insertion_at_buffered(app, out_buffer_id, 0, scratch, KB(64));
  buffer_set_setting(app, out_buffer_id, BufferSetting_ReadOnly, true);
  buffer_set_setting(app, out_buffer_id, BufferSetting_RecordsHistory, false);
  
  Temp_Memory buffer_name_restore_point = begin_temp(scratch);
  String_Const_u8 current_file_name = {};
  Buffer_ID current_buffer = 0;
  
  if (matches.first != 0){
    for (String_Match *node = matches.first;
         node != 0;
         node = node->next){
      if (node->buffer != out_buffer_id){
        if (current_buffer != 0 && current_buffer != node->buffer){
          insertc(&out, '\n');
        }
        if (current_buffer != node->buffer){
          end_temp(buffer_name_restore_point);
          current_buffer = node->buffer;
          current_file_name = push_buffer_file_name(app, scratch, current_buffer);
          if (current_file_name.size == 0){
            current_file_name = push_buffer_unique_name(app, scratch, current_buffer);
          }
          
          insertf(&out, "\n%.*s\n", string_expand(current_file_name));
        }
        
        Buffer_Cursor cursor = buffer_compute_cursor(app, current_buffer, seek_pos(node->range.first));
        Temp_Memory line_temp = begin_temp(scratch);
        String_Const_u8 full_line_str = push_buffer_line(app, scratch, current_buffer, cursor.line);
        String_Const_u8 line_str = string_skip_chop_whitespace(full_line_str);
        
        insertf(&out, "%d:%d: %.*s\n",
                cursor.line, cursor.col,
                string_expand(line_str));
        end_temp(line_temp);
      }
    }
  }
  else{
    insertf(&out, "no matches");
  }
  
  end_buffer_insertion(&out);
}
