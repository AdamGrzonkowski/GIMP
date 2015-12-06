/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Median filter
 * program written as an engineering thesis
 *
 * Copyright 2015 Adam S. Grzonkowski (adam.grzonkowski@wp.pl)
 *
 * You can share this code and modify it to suite your own needs.
 *
 */

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <stdlib.h>
#include <math.h>

/* Structure required for handling GUI */
typedef struct
{
  gint     radius;
  gboolean preview;
} median_InputValues;


// --------------------- //
// FUNCTION DECLARATIONS //
// --------------------- //
static void query       (void);
static void run         (const gchar      *name,
                         gint              number_of_input_params,
                         const GimpParam  *param,
                         gint             *nreturn_vals,
                         GimpParam       **return_vals);

static void median      (GimpDrawable     *drawable,
                         GimpPreview      *preview);

static void initialize_memory    (guchar         ***row,
                                  guchar          **outrow,
                                  gint              num_bytes);
static void process_row (guchar          **row,
                         guchar           *outrow,
                         gint              x1,
                         gint              y1,
                         gint              width,
                         gint              height,
                         gint              channels,
                         gint              i);

static int compare_numbers (const void *a, const void *b);

static void shuffle_tile_rows     (GimpPixelRgn     *rgn_in,
                         guchar          **row,
                         gint              x1,
                         gint              y1,
                         gint              width,
                         gint              height,
                         gint              ypos);

static gboolean median_dialog (GimpDrawable *drawable);

// Set up default values of GUI options
static median_InputValues InputValues =
{
  3,     // radius = 3
  1      // enable preview 
};

// Standard GIMP structure 
GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  //init not required
  NULL,  //quit not required
  query,
  run
};


MAIN()


// --------------------- //
//  SetUp Configuration  //
// --------------------- //
static void
query (void)
{
  /* Standard parameter definitions for filtering plug-in */
  static GimpParamDef args[] =
  {
    {
      GIMP_PDB_INT32,
      "run-mode",
      "Run mode"
    },
    {
      GIMP_PDB_IMAGE,
      "image",
      "Input image"
    },
    {
      GIMP_PDB_DRAWABLE,
      "drawable",                    //drawable = layers, layer masks, selections
      "Input drawable"
    }
  };

  gimp_install_procedure (
    "plug-in-median",                //name under which plug-in can be found in GIMP's Procedures Browser
    "Filtr medianowy",            
    "Usuwa plamki z obrazu",          
    "Adam S. Grzonkowski",            
    "Copyright Adam S. Grzonkowski",
    "2015",
    "_Filtr medianowy...",
    "RGB*, GRAY*",                   //image types handled by the plug-in
    GIMP_PLUGIN,                     //declaring this plug-in as external, not to be executed in GIMP core
    G_N_ELEMENTS (args), 0,
    args, NULL);

  gimp_plugin_menu_register ("plug-in-median",
                             "<Image>/Filters/Blur");  //menu path where plugin is registered
}

// --------------------- //
// Plug-in core function //
// --------------------- //
static void
run (const gchar      *name,
     gint              number_of_input_params,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam  values[1];
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpRunMode       run_mode;
  GimpDrawable     *drawable;

  // Setting mandatory output values 
  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  /* Getting run_mode - we won't display a dialog if 
   * we are in NONINTERACTIVE mode */
  run_mode = param[0].data.d_int32; 

  drawable = gimp_drawable_get (param[2].data.d_drawable);   // gets the specified drawable 

  /* Handling GIMP's all run modes */
  switch (run_mode)
    {
    case GIMP_RUN_INTERACTIVE:
      /* Get options last values if needed */
      gimp_get_data ("plug-in-median", &InputValues);

      /* Display the dialog */
      if (! median_dialog (drawable))
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      if (number_of_input_params != 4)
        status = GIMP_PDB_CALLING_ERROR;
      if (status == GIMP_PDB_SUCCESS)
        InputValues.radius = param[3].data.d_int32;
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      /*  Get options last values if needed  */
      gimp_get_data ("plug-in-median", &InputValues);
      break;

    default:
      break;
    }

  // Plug-ins main function call
  median (drawable, NULL);

  // These two functions send data to the gimp's core & update display
  gimp_displays_flush ();            
  gimp_drawable_detach (drawable);   

  //  Finally, set options in the core  
  if (run_mode == GIMP_RUN_INTERACTIVE)
    gimp_set_data ("plug-in-median", &InputValues, sizeof (median_InputValues));

  return;
}


// --------------------- //
//    Median filtering   //
// --------------------- //
static void
median (GimpDrawable *drawable,
      GimpPreview  *preview)
{
  gint         i, ii, channels;
  gint         x1, y1, x2, y2;
  GimpPixelRgn rgn_in, rgn_out;
  guchar     **row;
  guchar      *outrow;
  gint         width, height;

  if (! preview)
    gimp_progress_init ("Filtr medianowy..."); // initializes progress bar at the bottom of the screen
 

  // Gets upper left and lower right coordinates of the selected area of an image 
  if (preview)
    {
      gimp_preview_get_position (preview, &x1, &y1);
      gimp_preview_get_size (preview, &width, &height);
      x2 = x1 + width;
      y2 = y1 + height;
    }
  else
    {
      gimp_drawable_mask_bounds (drawable->drawable_id,
                                 &x1, &y1,
                                 &x2, &y2);
      width = x2 - x1;
      height = y2 - y1;
    }
  
  // Gets number of channels/layers for the specified drawable (returns bytes per pixel)
  channels = gimp_drawable_bpp (drawable->drawable_id);

  // Allocate a big enough tile cache. Tile has size of 64x64 pixels. We multiply *2 because of also processing shadow tiles
  // Increases performance significantly 
  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));

  /* Initialises two PixelRgns, one to read original data,
     and the other to write output data. That second one will
     be merged at the end by the call to
     gimp_drawable_merge_shadow() */
  gimp_pixel_rgn_init (&rgn_in,
                       drawable,
                       x1, y1,
                       width, height,
                       FALSE, FALSE);
  gimp_pixel_rgn_init (&rgn_out,
                       drawable,
                       x1, y1,
                       width, height,
                       preview == NULL, TRUE);

  // Allocate memory for input and output tile rows 
  initialize_memory (&row, &outrow, width * channels);

  // Gets pixels into the input rows: from (x,y) to (x+width-1,y)
  for (ii = -InputValues.radius; ii <= InputValues.radius; ii++)
    {
      gimp_pixel_rgn_get_row (&rgn_in,
                              row[InputValues.radius + ii],
                              x1, y1 + CLAMP (ii, 0, height - 1),
                              width);
    }

  // To be done for each tile row
  for (i = 0; i < height; i++)
    {
      process_row (row,
                   outrow,
                   x1, y1,
                   width, height,
                   channels,
                   i);
      // Sets pixels into to the output row
      gimp_pixel_rgn_set_row (&rgn_out,
                              outrow,
                              x1, i + y1,
                              width);
      // Shift tile rows to insert the new one at the end
      shuffle_tile_rows (&rgn_in,
              		 row,
               		 x1, y1,
              		 width, height,
               		 i);

      // Updates progress bar in GIMP's GUI
      if (! preview && i % 16 == 0)
        gimp_progress_update ((gdouble) i / (gdouble) height);
    }

  // Free the memory
  g_free (row);
  g_free (outrow);

  // Update the modified region 
  if (preview)
    {
      gimp_drawable_preview_draw_region (GIMP_DRAWABLE_PREVIEW (preview),
                                         &rgn_out);
    }
  else
    {
      gimp_drawable_flush (drawable);
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
      gimp_drawable_update (drawable->drawable_id,
                            x1, y1,
                            width, height);
    }
}


// -------------------------- //
// Allocates memory for input //
//    and output tile rows    //
// -------------------------- //
static void
initialize_memory (guchar ***row,
          guchar  **outrow,
          gint      num_bytes)
{
  gint i;

  *row = g_new (guchar *, (2 * InputValues.radius + 1));

  for (i = -InputValues.radius; i <= InputValues.radius; i++)
  {
    (*row)[i + InputValues.radius] = g_new (guchar, num_bytes);
  }

  *outrow = g_new (guchar, num_bytes);  
}

int compare_numbers (const void *a, const void *b)
{
   const gint *da = (const gint *) a;
   const gint *db = (const gint *) b;

  return (*da > *db) - (*da < *db);
}


// -------------------------- //
//    Process each tile row   //
// -------------------------- //
static void
process_row (guchar **row,
             guchar  *outrow,
             gint     x1,
             gint     y1,
             gint     width,
             gint     height,
             gint     channels,
             gint     i)
{
  gint j;
  gint *p_array;
  gint number_of_pixels = 4 * InputValues.radius * InputValues.radius + 4 * InputValues.radius + 1;  //(2r+1)x(2r+1)

  for (j = 0; j < width; j++)
    {
      gint k, ii, jj;
      gint left = (j - InputValues.radius),
           right = (j + InputValues.radius);    
      	 
      /* For each layer, perform median filtering of the
         (2r+1)x(2r+1) pixels */
      for (k = 0; k < channels; k++)
        {           
          p_array = calloc(number_of_pixels, sizeof(gint));
          gint sum = 0;
            
          for (ii = 0; ii < 2 * InputValues.radius + 1; ii++)
            for (jj = left; jj <= right; jj++)
              p_array[ii] = row[ii][channels * CLAMP (jj, 0, width - 1) + k]; //bierze wartosc piksela, CLAMP - obsługa krawędzi obraz
            
           //sortuje tablice 
          qsort(p_array, ii, sizeof(gint), compare_numbers);
          gint mid = floor(ii/2); //bierze wartosc srodkowa
 
          //zwraca wartosc mediany
          if ((ii % 2) == 1 )
            outrow[channels * j + k] = p_array[mid+1];
          else
            outrow[channels * j + k] = (p_array[mid]+p_array[mid+1])/2; 

          g_free(p_array);
        }
    }
}

// -------------------------- //
//   Shifts tile rows to put  //
//   the new one at the end   //
// -------------------------- //
static void
shuffle_tile_rows (GimpPixelRgn *rgn_in,
         guchar      **row,
         gint          x1,
         gint          y1,
         gint          width,
         gint          height,
         gint          ypos)
{
  gint    i;
  guchar *tmp_row;

  /* Get tile row (i + radius + 1) into row[0] */
  gimp_pixel_rgn_get_row (rgn_in,
                          row[0],
                          x1, MIN (ypos + InputValues.radius + y1, y1 + height - 1),
                          width);

  /* Permute row[i] with row[i-1] and row[0] with row[2r] */
  tmp_row = row[0];
  for (i = 1; i < 2 * InputValues.radius + 1; i++)
    row[i - 1] = row[i];
  row[2 * InputValues.radius] = tmp_row;
}

// -------------------------- //
//    Dialog window config    //
// -------------------------- //
static gboolean
median_dialog (GimpDrawable *drawable)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *main_hbox;
  GtkWidget *preview;
  GtkWidget *frame;
  GtkWidget *radius_label;
  GtkWidget *alignment;
  GtkWidget *spinbutton;
  GtkObject *spinbutton_adj;
  GtkWidget *frame_label;
  gboolean   run;

  gimp_ui_init ("median", FALSE);

  dialog = gimp_dialog_new ("Filtr medianowy", "median",
                            NULL, 0,
                            gimp_standard_help_func, "plug-in-median",

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  main_vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
  gtk_widget_show (main_vbox);

  preview = gimp_drawable_preview_new (drawable, &InputValues.preview);
  gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
  gtk_widget_show (preview);

  frame = gtk_frame_new (NULL);
  gtk_widget_show (frame);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);

  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (alignment);
  gtk_container_add (GTK_CONTAINER (frame), alignment);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 6, 6, 6, 6);

  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (main_hbox);
  gtk_container_add (GTK_CONTAINER (alignment), main_hbox);

  radius_label = gtk_label_new_with_mnemonic ("_Promień:");
  gtk_widget_show (radius_label);
  gtk_box_pack_start (GTK_BOX (main_hbox), radius_label, FALSE, FALSE, 6);
  gtk_label_set_justify (GTK_LABEL (radius_label), GTK_JUSTIFY_RIGHT);

  spinbutton = gimp_spin_button_new (&spinbutton_adj, InputValues.radius, 
                                     1, 32, 1, 1, 1, 5, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), spinbutton, FALSE, FALSE, 0);
  gtk_widget_show (spinbutton);

  frame_label = gtk_label_new ("<b>Zmień promień</b>");
  gtk_widget_show (frame_label);
  gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
  gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);

  g_signal_connect_swapped (preview, "invalidated",
                            G_CALLBACK (median),
                            drawable);
  g_signal_connect_swapped (spinbutton_adj, "value_changed",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);

  median (drawable, GIMP_PREVIEW (preview));

  g_signal_connect (spinbutton_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &InputValues.radius);
  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}

