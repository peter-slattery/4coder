// Inspired by Perky's Loco Yeet Sheets (which are way better named)
// Source: https://github.com/perky/4coder_loco

struct GS_Collection_Tunnel
{
  Buffer_ID src_buffer;
  Range_i64 src_range; // the range in the original code buffer
  Range_i64 dst_range; // the range in the collections buffer
};

struct GS_Collection_Tunnel_LUT
{
  i32* values;
  i32 len;
};

struct GS_Collection_Free_List
{
  i32 index;
  GS_Collection_Free_List* next;
};

struct GS_Collection_Tunnel_Table
{
  GS_Collection_Tunnel* tunnels;
  b8*                   tunnels_occupied;
  i32 tunnels_len;
  i32 tunnels_cap;
  
  GS_Collection_Tunnel_LUT* buffers;
  Buffer_ID*                buffers_owner_ids;
  i32 buffers_cap;
  
  GS_Collection_Free_List* first_free;
};

global GS_Collection_Tunnel_Table gs_collections;
global String_Const_u8 gs_collection_buffer_name = S8Lit("*collections*");

global bool gs_collection_buffer_locked = false;

function void
gs_collection_init(i32 buffers_cap, i32 tunnels_cap, Thread_Context* tctx)
{
  gs_collections.tunnels_cap = tunnels_cap;
  gs_collections.tunnels_len = 0;
  gs_collections.tunnels = base_array(
      tctx->allocator, GS_Collection_Tunnel, tunnels_cap
  );
  gs_collections.tunnels_occupied = base_array(
      tctx->allocator, b8, tunnels_cap
  );
  
  for (i32 i = 0; i < tunnels_cap; i++) gs_collections.tunnels_occupied[i] = false;
  
  gs_collections.buffers_cap = buffers_cap;
  gs_collections.buffers = base_array(
      tctx->allocator, GS_Collection_Tunnel_LUT, buffers_cap
  );
  gs_collections.buffers_owner_ids = base_array(
      tctx->allocator, Buffer_ID, buffers_cap
  );
  
  for (i32 i = 0; i < buffers_cap; i++)
  {
    gs_collections.buffers[i].values = base_array(
        tctx->allocator, i32, tunnels_cap
    );
    gs_collections.buffers[i].len = 0;
    gs_collections.buffers_owner_ids[i] = -1;
  }
}

function GS_Collection_Tunnel_LUT*
gs_collection_get_tunnel_lut(Buffer_ID buffer)
{
  GS_Collection_Tunnel_LUT* result = 0;
  i32 buffer_index = buffer % gs_collections.buffers_cap;
  if (gs_collections.buffers_owner_ids[buffer_index] == buffer ||
      gs_collections.buffers_owner_ids[buffer_index] == -1)
  {
    result = gs_collections.buffers + buffer_index;
    gs_collections.buffers_owner_ids[buffer_index] = buffer;
  }
  else
  {
    i32 at = (buffer_index + 1) % gs_collections.buffers_cap;
    while (gs_collections.buffers_owner_ids[at] != buffer_index &&
        gs_collections.buffers_owner_ids[at] != -1)
    {
      at = (at + 1) % gs_collections.buffers_cap;
      if (at == buffer_index) 
      {
        // NOTE(PS): if you hit this, you are trying to put tunnels
        // on too many buffers. 
        // Increase buffers_cap in gs_colection_init
        InvalidPath;
      }
    }
    
    Assert(gs_collections.buffers_owner_ids[at] == -1 ||
        gs_collections.buffers_owner_ids[at] == buffer);
    
    gs_collections.buffers_owner_ids[at] = buffer;
    result = gs_collections.buffers + at;
  }
  return result;
}

function GS_Collection_Tunnel*
gs_collection_get_next_tunnel_for_buffer(Buffer_ID buffer)
{
  i32 index = -1;
  GS_Collection_Tunnel* result = 0;
  if (gs_collections.first_free)
  {
    index = gs_collections.first_free->index;
    result = (GS_Collection_Tunnel*)gs_collections.first_free;
    gs_collections.first_free = gs_collections.first_free->next;
  }
  else
  {
    Assert(gs_collections.tunnels_len < gs_collections.tunnels_cap);
    index = gs_collections.tunnels_len++;
    result = gs_collections.tunnels + index;
  }
  gs_collections.tunnels_occupied[index] = true;
  
  GS_Collection_Tunnel_LUT* buf = gs_collection_get_tunnel_lut(buffer);
  Assert(buf->len < gs_collections.tunnels_cap);
  buf->values[buf->len++] = index;
  
  return result;
}

function Buffer_ID
gs_collection_buffer_get(Application_Links* app, bool force_create)
{
  Buffer_ID buffer_id = get_buffer_by_name(
      app, gs_collection_buffer_name, Access_Always
  );
  if (!buffer_exists(app, buffer_id) && force_create)
  {
    buffer_id = create_buffer(
        app, gs_collection_buffer_name, BufferCreate_AlwaysNew
    );
    buffer_set_setting(app, buffer_id, BufferSetting_Unimportant, true);
    // TODO(PS): set the buffer to be treated as code
  }
  return buffer_id;
}

function void
gs_collection_remove_tunnel(Application_Links* app, Buffer_ID buffer, i32 index_in_buffer)
{
  GS_Collection_Tunnel_LUT* buf_lut = gs_collection_get_tunnel_lut(buffer);
  i32 index = buf_lut->values[index_in_buffer];
  GS_Collection_Tunnel* tunnel = gs_collections.tunnels + index;
  
  // replace in lut
  buf_lut[index_in_buffer] = buf_lut[--buf_lut->len];
  
  // remove tunnel from c_buffer
  // lock the buffer here to prevent erasing the c_buffer from 
  // causing a knock on erase operation on the source
  gs_collection_buffer_locked = true;
  Buffer_ID c_buffer = gs_collection_buffer_get(app, false);
  buffer_replace_range(app, c_buffer, tunnel->dst_range, String_Const_u8{0});
  gs_collection_buffer_locked = false;
  
  // remove tunnel from tracking structure
  gs_collections.tunnels_occupied[index] = false;
  GS_Collection_Free_List* new_free = (GS_Collection_Free_List*)tunnel;
  new_free->index = index;
  new_free->next = gs_collections.first_free;
  gs_collections.first_free = new_free;
}

function void
gs_collection_view_show_(Application_Links* app, View_ID view, Buffer_ID c_buffer, Range_i64 pos)
{
  view_set_buffer(app, view, c_buffer, 0);
  view_set_cursor_and_preferred_x(app, view, seek_pos(pos.min));
}

function void
gs_collection_view_show(Application_Links* app, View_ID view)
{
  Buffer_ID c_buffer = gs_collection_buffer_get(app, true);
  gs_collection_view_show_(app, view, c_buffer, Range_i64{0,0});
}

function Range_i64
gs_collection_copy_text_to_buffer(
    Application_Links* app,
  Arena* arena,
  Buffer_ID src_buffer,
  Buffer_ID dst_buffer,
  Range_i64 src_range,
  Range_i64 dst_range
){
  String_Const_u8 copy_string = push_buffer_range(app, arena, src_buffer, src_range);
  buffer_replace_range(app, dst_buffer, dst_range, copy_string);
  return Ii64(dst_range.min, dst_range.min + copy_string.size);
}

// Returns the insertion range in dst_buffer
function Range_i64
gs_collection_copy_text_to_buffer(
    Application_Links* app,
  Arena* arena,
  Buffer_ID src_buffer,
  Buffer_ID dst_buffer,
  Range_i64 src_range
){
  String_Const_u8 copy_string = push_buffer_range(app, arena, src_buffer, src_range);
  
  i64 dst_insert_start = (i64)buffer_get_size(app, dst_buffer);
  
  // TODO(PS): lock_buffer = true;
  Buffer_Insertion insert = begin_buffer_insertion_at_buffered(app, dst_buffer, dst_insert_start, arena, KB(16));
  insertc(&insert, '\n');
  insert_string(&insert, copy_string);
  insertc(&insert, '\n');
  insertc(&insert, '\n');
  end_buffer_insertion(&insert);
  
  i64 dst_insert_end = (i64)buffer_get_size(app, dst_buffer);
  
  // +1 to ignore start newline.
  // -2 to ignore the two end newlines.
  return Ii64(dst_insert_start + 1, dst_insert_end - 2);
}

function void
gs_collection_collect_range(
    Application_Links* app, 
  Buffer_ID src_buffer, 
  Range_i64 range
){
  Buffer_ID c_buffer = gs_collection_buffer_get(app, true);
  if (src_buffer == c_buffer) return; // TODO(PS): check if cursor is inside
  
  // If the new range overlaps with an existing range, then we want to combine them
  GS_Collection_Tunnel_LUT* buf_lut = gs_collection_get_tunnel_lut(src_buffer);
  GS_Collection_Tunnel* existing_tunnel = 0;
  i32 i = 0;
  for (; i < buf_lut->len; i++)
  {
    i32 index = buf_lut->values[i];
    GS_Collection_Tunnel* t = gs_collections.tunnels + index;
    if (range_overlap(t->src_range, range))
    {
      t->src_range = range_union(t->src_range, range);
      range = t->src_range;
      existing_tunnel = t;
      break;
    }
  }
  
  Range_i64 insertion_range = {};
  
  Scratch_Block scratch(app);
  if (!existing_tunnel)
  {
    // Copy the range from src_buffer to c_buffer
    insertion_range = gs_collection_copy_text_to_buffer(app, scratch, src_buffer, c_buffer, range);
    
    GS_Collection_Tunnel* tunnel = gs_collection_get_next_tunnel_for_buffer(src_buffer);
    tunnel->src_buffer = src_buffer;
    tunnel->src_range = range;
    tunnel->dst_range = insertion_range;
  }
  else
  {
    // Check to see if the new range overlaps with any
    // other existing ranges. Because the list is maintained
    // in a state where no ranges overlap one another, at this
    // point all that is needed is to do one pass over the entire
    // list to see if there are any that get consumed into the 
    // region just grown. Because none of the remaining regions 
    // overlapped eachother to begin with, combining them into this
    // new region will not affect any other regions overlap state, 
    // preventing hte need for multiple iterations
    // Further, because we checked [0:i] tunnels already, none of which
    // overlapped with the new range, and by definition, none of which
    // overlap with existing_tunnel, we don't need to check them again
    for (i32 j = buf_lut->len - 1; j > i; j--)
    {
      i32 index = buf_lut->values[j];
      GS_Collection_Tunnel* t = gs_collections.tunnels + index;
      if (range_overlap(t->src_range, range))
      {
        existing_tunnel->src_range = range_union(existing_tunnel->src_range, t->src_range);
        gs_collection_remove_tunnel(app, src_buffer, j);
      }
    }
    
    insertion_range = gs_collection_copy_text_to_buffer(app, scratch, src_buffer, c_buffer, range, existing_tunnel->dst_range);
    existing_tunnel->dst_range = insertion_range;
  }
  
  View_ID c_view = get_next_view_after_active(app, Access_Always);
  gs_collection_view_show_(app, c_view, c_buffer, insertion_range);
}

////////////////////////////////////////////////////
// Hooks

function bool
gs_collection_on_buffer_edit(
    Application_Links *app, 
  Buffer_ID buffer_id, 
  Range_i64 old_range, 
  Range_i64 new_range
){
  if (gs_collections.tunnels_len == 0) return false;
  
  Scratch_Block scratch(app);
  Buffer_ID c_buffer = gs_collection_buffer_get(app, false);
  if (buffer_id == c_buffer)
  {
    if (gs_collection_buffer_locked) return false;
    
    // TODO(PS): would be great to have a way to jump to 
    // the start buffer. All subsequent ones need to be edited
    // but it would prevent iterating through them all 
    i32 i = 0;
    GS_Collection_Tunnel* edit_tunnel = 0;
    for (; i < gs_collections.tunnels_len; i++)
    {
      if (!gs_collections.tunnels_occupied[i]) continue;
      GS_Collection_Tunnel* at = gs_collections.tunnels + i;
      if (old_range.min >= at->dst_range.min && old_range.max <= at->dst_range.max)
      {
        edit_tunnel = at;
        break;
      }
    }
    
    if (edit_tunnel) 
    {
      gs_collection_buffer_locked = true;
      
      i32 edit_tunnel_index = i;
      
      // Update the tunnel being edited
      i64 size_delta = range_size(new_range) - range_size(old_range);
      edit_tunnel->dst_range.max += size_delta;
      
      String_Const_u8 string = push_buffer_range(app, scratch, c_buffer, edit_tunnel->dst_range);
      buffer_replace_range(app, edit_tunnel->src_buffer, edit_tunnel->src_range, string);
      edit_tunnel->src_range.max += size_delta;
      
      // Update all tunnels that follow
      for (i = i + 1; i < gs_collections.tunnels_len; i++)
      {
        if (!gs_collections.tunnels_occupied[i]) continue;
        GS_Collection_Tunnel* at = gs_collections.tunnels + i;
        at->dst_range.min += size_delta;
        at->dst_range.max += size_delta;
        
        if (at->src_buffer == edit_tunnel->src_buffer &&
            at->src_range.min >= edit_tunnel->src_range.max)
        {
          at->src_range.min += size_delta;
          at->src_range.max += size_delta;
        }
      }
      
      if (range_size(edit_tunnel->dst_range) == 0)
      {
        gs_collection_remove_tunnel(app, edit_tunnel->src_buffer, edit_tunnel_index);
      }
      gs_collection_buffer_locked = false;
    }
  }
  else if (!gs_collection_buffer_locked)
  {
    GS_Collection_Tunnel_LUT* buf_lut = gs_collection_get_tunnel_lut(buffer_id);
    i32 i = 0;
    GS_Collection_Tunnel* edit_tunnel = 0;
    for (; i < buf_lut->len; i++)
    {
      i32 index = buf_lut->values[i];
      GS_Collection_Tunnel* at = gs_collections.tunnels + index;
      if (old_range.min >= at->src_range.min && old_range.max <= at->src_range.max)
      {
        edit_tunnel = at;
        break;
      }
    }
    
    if (edit_tunnel) 
    {
      gs_collection_buffer_locked = true;
      
      // Update the tunnel being edited
      i64 size_delta = range_size(new_range) - range_size(old_range);
      edit_tunnel->src_range.max += size_delta;
      
      // TODO(PS): why do we need another scratch block?
      String_Const_u8 string = push_buffer_range(app, scratch, buffer_id, edit_tunnel->src_range);
      buffer_replace_range(app, c_buffer, edit_tunnel->dst_range, string);
      edit_tunnel->dst_range.max += size_delta;
      
      // Update all tunnels that follow
      for (i = i + 1; i < gs_collections.tunnels_len; i++)
      {
        if (!gs_collections.tunnels_occupied[i]) continue;
        i32 index = buf_lut->values[i];
        GS_Collection_Tunnel* at = gs_collections.tunnels + index;
        if (at->src_range.min > edit_tunnel->src_range.max)
        {
          at->src_range.min += size_delta;
          at->src_range.max += size_delta;
        }
        
        if (at->dst_range.min >= edit_tunnel->dst_range.max)
        {
          at->dst_range.min += size_delta;
          at->dst_range.max += size_delta;
        }
      }
      
      gs_collection_buffer_locked = false;
    }
  }
  
  return true;
}

function bool
gs_collection_buffer_render(
    Application_Links* app, 
  View_ID view_id, 
  Face_ID face_id, 
  Buffer_ID buffer, 
  Text_Layout_ID text_layout_id,
  Rect_f32 rect)
{
  Buffer_ID c_buffer = get_buffer_by_name(
      app, gs_collection_buffer_name, Access_Always
  );
  if (!buffer_exists(app, c_buffer)) return false;
  if (buffer != c_buffer) return false;
  
  Scratch_Block scratch(app);
  f32 line_height = get_view_line_height(app, view_id);
  FColor comment_color = fcolor_id(defcolor_comment);
  
  for (i32 i = 0; i < gs_collections.tunnels_len; i++)
  {
    if (!gs_collections.tunnels_occupied[i]) continue;
    GS_Collection_Tunnel tunnel = gs_collections.tunnels[i];
    if (!buffer_exists(app, tunnel.src_buffer)) continue;
    
    i64 start_line = get_line_number_from_pos(
        app, tunnel.src_buffer, tunnel.src_range.min
    );
    i64 end_line = get_line_number_from_pos(
        app, tunnel.src_buffer, tunnel.src_range.max
    );
    
    String_Const_u8 unique_name = push_buffer_unique_name(
        app, scratch, tunnel.src_buffer
    );
    
    Fancy_Line pre_line = {};
    push_fancy_stringf(scratch, &pre_line, comment_color, "// ");
    push_fancy_string(scratch, &pre_line, comment_color, unique_name);
    push_fancy_stringf(scratch, &pre_line, comment_color, " - Lines: %3.lld - %3.lld", start_line, end_line);
    
    Fancy_Line post_line = {};
    push_fancy_stringf(scratch, &post_line, comment_color, "// end");
    
    // TODO(PS): get this to use all the syntax highlighting etc
    // stuff we built in gs_render_buffer
    Rect_f32 start_rect = text_layout_character_on_screen(
        app, text_layout_id, tunnel.dst_range.min
    );
    Rect_f32 end_rect = text_layout_character_on_screen(
        app, text_layout_id, tunnel.dst_range.max
    );
    
    Vec2_f32 comment_pos_pre = { rect.x0, start_rect.y0 - line_height };
    draw_fancy_line(app, face_id, comment_color, &pre_line, comment_pos_pre);
    
    Vec2_f32 comment_pos_post = { rect.x0, end_rect.y0 + line_height };
    draw_fancy_line(app, face_id, comment_color, &post_line, comment_pos_post);
  }
  
  return true;
}

////////////////////////////////////////////////////
// Commands

CUSTOM_COMMAND_SIG(gs_collection_collect_cursor_range)
CUSTOM_DOC("Creates a tunnel from the selected region (marked by the mark and cursor pair) to the active collection")
{
  View_ID view = get_active_view(app, Access_Always);
  Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
  Range_i64 range = get_view_range(app, view);
  gs_collection_collect_range(app, buffer, range);
}

CUSTOM_COMMAND_SIG(gs_collection_clear)
CUSTOM_DOC("Clears all active tunnels")
{
  // Clear collection tracking
  gs_collections.tunnels_len = 0;
  for (i32 i = 0; i < gs_collections.tunnels_cap; i++) 
  {
    gs_collections.tunnels_occupied[i] = false;
  }
  for (i32 i = 0; i < gs_collections.buffers_cap; i++)
  {
    gs_collections.buffers[i].len = 0;
  }
  
  // Clear c_buffer contents
  Buffer_ID c_buffer = gs_collection_buffer_get(app, false);
  i64 c_buffer_size = buffer_get_size(app, c_buffer);
  Range_i64 c_buffer_range = { 0, c_buffer_size };
  String_Const_u8 string = {};
  buffer_replace_range(app, c_buffer, c_buffer_range, string);
}