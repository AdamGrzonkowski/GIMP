/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * Median filter
 * program written as an engineering thesis
 *
 * Copyright 2015 Adam S. Grzonkowski (adam.grzonkowski@wp.eu)
 *
 */

//https://www.gimp.org/docs/plug-in/plug-in.html 

/* Include libraries */
#include <libgimp/gimp.h>   // For application logic functions
#include <libgimp/gimpui.h> // For application UI functions
#include <math.h>           // Required only for floor()


/* Structure required for handling GUI */
typedef struct
{
  gint     radius;
  gboolean preview;
  gint     lessThan;
  gint     greaterThan;
  gboolean button;
  gboolean button2;
} MedianInputValues;


// --------------------- //
// FUNCTION DECLARATIONS //
// --------------------- //
static inline void query       (void);
static inline void run         (const gchar      *name,
                         gint              numberOfInputParams,
                         const GimpParam  *inputValues,
                         gint             *numberOfOutputParams,
                         GimpParam       **returnValues);

static inline void median      (GimpDrawable     *drawable,
                         GimpPreview      *preview);

static inline void initializeMemory  (guchar         ***inputRow,
                               guchar          **outputRow,
                               gint              num_bytes);
static inline void handleInputRow    (guchar           **inputRow,
                               guchar           *outputRow,
                               gint              width,
                               gint              channels);

static inline gint compareNumbers (const void *a, const void *b);

static inline void shuffle_tile_rows     (GimpPixelRgn     *rgn_in,
                         guchar          **inputRow,
                         gint              x1,
                         gint              y1,
                         gint              width,
                         gint              height,
                         gint              ypos);

static inline gboolean medianDialog (GimpDrawable *drawable);

/* Set up default values of GUI options */
static MedianInputValues UserInputValues =
{
  2,     // radius = 2
  1,     // enable preview 
  0,     // default lessThan filtering variant value
  0,     // default greaterThan filtering variant value
  FALSE, // set button filtering variant value to false
  FALSE  // set button2 filtering variant value to false
};

/* Standard GIMP structure */
GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  //init() not required
  NULL,  //quit() not required
  query,
  run
};


/* Initializes arguments, calls PLUG_IN_INFO, 
   sets communication between plug-in & PDB */
MAIN()


// ------------------------- //
//  Register plug-in in PDB  //
// ------------------------- //
static inline void 
query (void)
{
  /* Standard input parameters definitions */
  static GimpParamDef pluginInputParams[] =
  {
    { // Gets run mode of GIMP (interactive or non-interactive)
      GIMP_PDB_INT32, // Type
      "run-mode",     // Name
      "Run mode"      // Description
    },
    { // Gets image itself 
      GIMP_PDB_IMAGE,
      "image",
      "Input image"
    },
    { // Gets info about pixels, layers, layer masks, selections
      GIMP_PDB_DRAWABLE,
      "drawable",                    
      "Input drawable"
    }
  };
  // Register plug-in in PDB
  gimp_install_procedure (
    "plug-in-median",                 // name registered in memory
    "Filtr medianowy",                // name displayed GIMP's Procedures Browser
    "Usuwa plamki z obrazu",          // plug-in's description 
    "Adam S. Grzonkowski",            // author
    "Copyright Adam S. Grzonkowski",  // copyright
    "2015",                           // year
    "_Filtr medianowy...",            // name displayed in GIMP's UI
    "RGB*, GRAY*",                    // image types handled by the plug-in
    GIMP_PLUGIN,                      // declaring this plug-in as external, not to be executed in GIMP core
    G_N_ELEMENTS (pluginInputParams), // number of plugin's input values (from GimpParamDef)
    0,                                // number of return values
    pluginInputParams,                // input values (from GimpParamDef)
    NULL);                            // return values

  // Register plug-in in GIMP's UI
  gimp_plugin_menu_register ("plug-in-median",
                             "<Image>/Filters/Enhance");  //menu path of plug-in
}


// --------------------- //
// Plug-in core function //
// --------------------- //
static inline void
run (const gchar      *name,
     gint              numberOfInputParams,
     const GimpParam  *inputValues,
     gint             *numberOfOutputParams,
     GimpParam       **returnValues)
{
  // Declaring local variables
  static GimpParam  values[1];                 // Need one element to store status
  GimpPDBStatusType status = GIMP_PDB_SUCCESS; // Set type of status
  GimpRunMode       run_mode;                  // Stores info about run-mode
  GimpDrawable     *drawable;                  // Pointer to drawable

  // Set mandatory output value - status of plug-in
  *numberOfOutputParams = 1;
  *returnValues  = values;
  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  run_mode = inputValues[0].data.d_int32;      // Get run_mode

  drawable = gimp_drawable_get (inputValues[2].data.d_drawable); // Get drawable

  switch (run_mode)                                       // Handle GIMP's all run modes 
    {
    case GIMP_RUN_INTERACTIVE:
      gimp_get_data ("plug-in-median", &UserInputValues); // Get last chosen options in plug-in's GUI
      if (! medianDialog (drawable))		          // Display the dialog window
        return;
      break;

    case GIMP_RUN_NONINTERACTIVE:
      if (numberOfInputParams != 4)                           // If not enough input values
        status = GIMP_PDB_CALLING_ERROR;                      // call error
      if (status == GIMP_PDB_SUCCESS)                         // If you have all you need
        UserInputValues.radius = inputValues[3].data.d_int32; // Get radius' default value
      break;

    case GIMP_RUN_WITH_LAST_VALS:
      gimp_get_data ("plug-in-median", &UserInputValues);     // Run with last chosen options
      break;

    default:
      break;
    }

  median (drawable, NULL);  // Call to median function

  // These two functions send data to the gimp's core & update display
  gimp_displays_flush ();            
  gimp_drawable_detach (drawable);   

  //  Remember options chosen in GUI
  if (run_mode == GIMP_RUN_INTERACTIVE)
    gimp_set_data ("plug-in-median", &UserInputValues, sizeof (MedianInputValues));

  return;
}


// --------------------- //
//    Median filtering   //
// --------------------- //
static inline void
median (GimpDrawable *drawable,
        GimpPreview  *preview)
{
  gint         i, ii, channels;
  gint         x1, y1, x2, y2;
  GimpPixelRgn rgn_in, rgn_out;
  guchar     **inputRow;
  guchar      *outputRow;
  gint         width, height;

  // If there's no preview
  if (! preview)
    gimp_progress_init ("Filtr medianowy..."); // initialize progress bar at the bottom of the screen
 
  /* If preview is active, get upper left and lower right 
     coordinates of the selected area of an image and
     set preview size accordingly to width & height of selection */
  if (preview)
    {
      gimp_preview_get_position (preview, &x1, &y1);   
      gimp_preview_get_size (preview, &width, &height);
      x2 = x1 + width;
      y2 = y1 + height;
    }
  else
    {
      /* Find the bounding box of the current selection 
         in relation to the specified drawable */
      gimp_drawable_mask_bounds (drawable->drawable_id,
                                 &x1, &y1,
                                 &x2, &y2);
      width = x2 - x1;
      height = y2 - y1;
    }
  
  // Get number of channels for the specified drawable (returns bytes per pixel)
  channels = gimp_drawable_bpp (drawable->drawable_id);

  /* Allocate a big enough tile cache.
     Multiply *2 because of also processing shadow tiles
     Increases performance significantly */ 
  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width() + 1));

  /* Initialise two pixel ranges, one to read input data,
     and the other to write output data. */
  gimp_pixel_rgn_init (&rgn_in,          
                       drawable,
                       x1, y1,
                       width, height,
                       FALSE, FALSE); // 2*FALSE = the range is only used to read data
  gimp_pixel_rgn_init (&rgn_out,
                       drawable,
                       x1, y1,
                       width, height,
                       preview == NULL, TRUE); // the range is used to write data

  // Allocate memory for tile inputRow and outputRow
  initializeMemory (&inputRow, &outputRow, width * channels);

  // Gets pixels into the tile input rows from (2r+1)*(2r+1) matrix. ii - controls height
  for (ii = -UserInputValues.radius; ii <= UserInputValues.radius; ii++)
    {
      gimp_pixel_rgn_get_row (&rgn_in,
                              inputRow[UserInputValues.radius + ii],
                              x1, y1 + CLAMP (ii, 0, height - 1),
                              width);
    }

  // To be done for all tile input rows
  for (i = 0; i < height; i++)
    {
      // Perform the actual median filtering
      handleInputRow  (inputRow,
                       outputRow,
                       width,
                       channels);
      // Set pixels into to the outputRow
      gimp_pixel_rgn_set_row (&rgn_out,
                              outputRow,
                              x1, i + y1,
                              width);
      // Shift tile rows to insert the new one at the end
      shuffle_tile_rows (&rgn_in,
              		 inputRow,
               		 x1, y1,
              		 width, height,
               		 i);

      // Update progress bar in GIMP's GUI
      if (! preview && i % 16 == 0)
        gimp_progress_update ((gdouble) i / (gdouble) height);
    }

  // Free the memory
  g_free (inputRow);
  g_free (outputRow);

  // Update the modified region on preview
  if (preview)
    {
      gimp_drawable_preview_draw_region (GIMP_DRAWABLE_PREVIEW (preview),
                                         &rgn_out);
    }
  else
    {
      gimp_drawable_flush (drawable);                           // send tile data to the core and get results 
      gimp_drawable_merge_shadow (drawable->drawable_id, TRUE); // merge shadow buffer with drawable
      gimp_drawable_update (drawable->drawable_id,              // update the processed region of drawable
                            x1, y1,
                            width, height);
    }
}


// -------------------------- //
// Allocates memory for input //
//       and output rows      //
// -------------------------- //
static inline void
initializeMemory (guchar ***inputRow,
          guchar  **outputRow,
          gint      num_bytes)
{
  gint i;

  // Allocate memory for one input row
  *inputRow = g_new (guchar*, (2 * UserInputValues.radius + 1));

  // Now go from bottom to top and allocate enough memory for all input rows in input matrix
  for (i = -UserInputValues.radius; i <= UserInputValues.radius; i++)
  {
    (*inputRow)[i + UserInputValues.radius] = g_new (guchar, num_bytes);
  }
  // Allocate memory for output row (need only enough for one)
  *outputRow = g_new (guchar, num_bytes);  
}


// -------------------------- //
//     Compares two numbers   //
//    used in sort algorithm  //
// -------------------------- //
static inline gint 
compareNumbers (const void *a, const void *b)
{
   const gint *da = (const gint *) a;
   const gint *db = (const gint *) b;

  return (*da > *db) - (*da < *db);
}


// -------------------------- //
//    Sorts the given array   //
//  using Heapsort algorithm  //
// in time O(n), memory O(1)  //
// -------------------------- //
static inline void 
heapSort(gint array[], gint N) 
{
  gint n = N, i = n/2, parent, child;
  gint t;
  //
  for (;;) { 		   // Loops until array is sorted 
    if (i > 0) { 	   // First stage - Sorting the heap 
      i--;                 // Save its index to i 
      t = array[i];        // Save parent value to t 
    } else {               // Second stage - Extracting elements in-place 
      n--;                 // Make the new heap smaller 
      if (n == 0) return;  // When the heap is empty, we are done 
      t = array[n];        // Save last value (it will be overwritten) 
      array[n] = array[0]; // Save largest value at the end of array
    }
 
    parent = i;            // We will start pushing down t from parent
    child = i*2 + 1;       // parent's left child
 
    // Sift operation - pushing the value of t down the heap
    while (child < n) {
      if (child + 1 < n  &&  array[child + 1] > array[child]) {
	child++;                      // Choose the largest child
      }
      if (array[child] > t) {         // If any child is bigger than the parent
	array[parent] = array[child]; // Move the largest child up
	parent = child;               // Move parent pointer to this child
	child = parent*2 + 1;         // Find the next child
      } else {
	break;                        // t's place is found
      }
    }
    array[parent] = t;                // We save t in the heap
  }
}


// ------------------------------ //
//   Process each tile inputRow   //
// ------------------------------ //
static inline void
handleInputRow (guchar **inputRow,
             guchar  *outputRow,
             gint     width,
             gint     channels)
{
  gint j;
  gint numberOfPixels = 4 * UserInputValues.radius * UserInputValues.radius + 4 * UserInputValues.radius + 1;  //(2r+1)x(2r+1)
  gint *pixelsArray = g_new (gint, numberOfPixels);  // Allocate enough memory for local array of pixels
  gint result = 0;
  gint medianResult = 0;

  for (j = 0; j < width; j++)    // For the whole inputRow
    {
      gint k, ii, jj;
      gint left = (j - UserInputValues.radius),
           right = (j + UserInputValues.radius);    
      	 
      /* For each layer, perform median filtering of the
         (2r+1)x(2r+1) pixels */
      for (k = 0; k < channels; k++)
        {           	  
          gint index = 0; // it serves as index of local array
         
          for (ii = 0; ii < 2 * UserInputValues.radius + 1; ii++)  // For all tile rows in a given height
            for (jj = left; jj <= right; jj++)                     // process each tile inputRow in a given width
            { 
	      // Assigns pixel value; CLAMP prevents going over image edges   
              pixelsArray[index] = inputRow[ii][channels * CLAMP (jj, 0, width - 1) + k]; 
              index += 1;
            }

          // Gets the currently worked on pixel
          gint middlePosition = floor(numberOfPixels / 2); 
          gint middlePixel = pixelsArray[middlePosition];

          // Sorts pixels and gets median value of the array
	  //heapSort(pixelsArray, numberOfPixels); // for img 512x512px & r=10, t=30,71s
          qsort(pixelsArray, numberOfPixels, sizeof(gint), compareNumbers); // for img 512x512px & r=10, t=23,45s
          gint mid = floor(numberOfPixels / 2);
 
          // Returns median value of the given neighbour pixels
          if ((numberOfPixels % 2) == 1 )
            medianResult = pixelsArray[mid+1];
          else
            medianResult = (pixelsArray[mid] + pixelsArray[mid+1]) / 2;

          // Check variants of filtering
	  if (UserInputValues.lessThan != 0 && UserInputValues.greaterThan == 0 && UserInputValues.button && !UserInputValues.button2)
	  {
             if (middlePixel < (medianResult - UserInputValues.lessThan))
             	result = medianResult;
             else
                result = middlePixel;
          }
          else if (UserInputValues.lessThan == 0 && UserInputValues.greaterThan != 0 && !UserInputValues.button && UserInputValues.button2)
          {
             if (middlePixel > (medianResult + UserInputValues.greaterThan))
             	result = medianResult;
             else
                result = middlePixel;
          }
	  else if (UserInputValues.lessThan != 0 && UserInputValues.greaterThan != 0 && !UserInputValues.button2 && !UserInputValues.button)
          {
             if (middlePixel >= (medianResult - UserInputValues.lessThan) && middlePixel <= (medianResult + UserInputValues.greaterThan))
             	result = medianResult;
             else
                result = middlePixel;
          }
          else if (UserInputValues.lessThan != 0 && UserInputValues.greaterThan != 0 && UserInputValues.button2 && UserInputValues.button)
          {
             if (middlePixel < (medianResult - UserInputValues.lessThan) || middlePixel > (medianResult + UserInputValues.greaterThan))
             	result = medianResult;
             else
                result = middlePixel;
          }
          else
	     result = medianResult;

          // Return result of median filtering
          outputRow[channels * j + k] = result;  
        }
    }
  g_free(pixelsArray); // Free memomy of local array
}


// -------------------------- //
//   Shifts tile rows to put  //
//   the new one at the end   //
// -------------------------- //
static inline void
shuffle_tile_rows (GimpPixelRgn *rgn_in,
         guchar      **inputRow,
         gint          x1,
         gint          y1,
         gint          width,
         gint          height,
         gint          ypos)
{
  gint    i;
  guchar *tmp_inputRow;

  // Get tile inputRow (i + radius + 1) into inputRow[0]
  gimp_pixel_rgn_get_row (rgn_in,
                          inputRow[0],
                          x1, MIN (ypos + UserInputValues.radius + y1, y1 + height - 1),
                          width);

  // Shift inputRow[i] with inputRow[i-1] and inputRow[0] with inputRow[2*radius] 
  tmp_inputRow = inputRow[0];
  for (i = 1; i < 2 * UserInputValues.radius + 1; i++)
    inputRow[i-1] = inputRow[i];
  inputRow[2 * UserInputValues.radius] = tmp_inputRow;
}


// -------------------------- //
//    Dialog window config    //
// -------------------------- //
static inline gboolean
medianDialog (GimpDrawable *drawable)
{
  GtkWidget *dialog;
  GtkWidget *main_vbox;
  GtkWidget *main_hbox;
  GtkWidget *second_hbox;
  GtkWidget *preview;
  GtkWidget *frame;
  GtkWidget *frame2;
  GtkWidget *radius_label;
  GtkWidget *alignment;
  GtkWidget *alignment2;
  GtkWidget *spinbutton;
  GtkObject *spinbutton_adj;
  GtkWidget *spinbutton2;
  GtkWidget *spinbutton2_label;
  GtkObject *spinbutton_adj2;
  GtkWidget *spinbutton3;
  GtkWidget *spinbutton3_label;
  GtkWidget *spinbutton3_label2;
  GtkObject *spinbutton_adj3;
  GtkWidget *frame_label;
  GtkWidget *frame_label2;
  GtkWidget *hints;
  GtkWidget *button;
  GtkWidget *button2;
  gboolean   run;

  gimp_ui_init ("median", FALSE);  // initialise GTK+, does all the magic so the 
                                   // plugin's UI would look like GIMP's core function

  // hooks the dialog to median plugin
  dialog = gimp_dialog_new ("Filtr medianowy", "median",
                            NULL, 0,
                            gimp_standard_help_func, "plug-in-median",
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,
                            NULL); 
  
  // Set vertical container main_vbox and add the hooked dialog widget to it
  main_vbox = gtk_vbox_new (FALSE, 6);   
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), main_vbox);
  gtk_widget_show (main_vbox); // show box

  // Create preview widget for drawable and add it to main_vbox
  preview = gimp_drawable_preview_new (drawable, &UserInputValues.preview);
  gtk_box_pack_start (GTK_BOX (main_vbox), preview, TRUE, TRUE, 0);
  gtk_widget_show (preview); // show preview

  // Display hint for a user
  hints = gimp_hint_box_new ("UWAGA!\nDziałanie filtru dla promienia r > 6 \nmoże być wolne.");
  gtk_box_pack_end (GTK_BOX (main_vbox), hints, FALSE, FALSE, 0);

  // Create frame and add it to main_vbox
  frame = gtk_frame_new (NULL);
  gtk_widget_show (frame);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);

  // Set padding 
  alignment = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (alignment);
  gtk_container_add (GTK_CONTAINER (frame), alignment);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 6, 6, 6, 6);

  // Create new horizontal box
  main_hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (main_hbox);
  gtk_container_add (GTK_CONTAINER (alignment), main_hbox);

  // Create label for spin button to set radius
  radius_label = gtk_label_new_with_mnemonic ("_Promień:");
  gtk_widget_show (radius_label);
  gtk_box_pack_start (GTK_BOX (main_hbox), radius_label, FALSE, FALSE, 6);
  gtk_label_set_justify (GTK_LABEL (radius_label), GTK_JUSTIFY_RIGHT);

  // Add spin button to allow setting radius and hook it to UserInputValues
  spinbutton = gimp_spin_button_new (&spinbutton_adj, UserInputValues.radius, 
                                     1, 30, 1, 1, 1, 5, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), spinbutton, FALSE, FALSE, 0);
  gtk_widget_show (spinbutton);

  // Add label to the previously created frame
  frame_label = gtk_label_new ("<b>Zmień promień</b>");
  gtk_widget_show (frame_label);
  gtk_frame_set_label_widget (GTK_FRAME (frame), frame_label);
  gtk_label_set_use_markup (GTK_LABEL (frame_label), TRUE);

  // Create frame and add it to main_vbox
  frame2 = gtk_frame_new (NULL);
  gtk_widget_show (frame2);
  gtk_box_pack_start (GTK_BOX (main_vbox), frame2, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (frame2), 6);

    // Set padding 
  alignment2 = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (alignment2);
  gtk_container_add (GTK_CONTAINER (frame2), alignment2);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment2), 6, 6, 6, 6);

  // Create new horizontal box to hold filter variants
  second_hbox = gtk_hbox_new (FALSE, 0);
  gtk_widget_show (second_hbox);
  gtk_container_add (GTK_CONTAINER (alignment2), second_hbox);

  // Create checkbox for variant filtering
  button = gtk_check_button_new();
  gtk_box_pack_start (GTK_BOX (second_hbox), button, FALSE, FALSE, 3);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), UserInputValues.button);

  // Create spin button to set lessThan variable
  spinbutton2_label = gtk_label_new_with_mnemonic ("_x < m -");
  gtk_widget_show (spinbutton2_label);
  gtk_box_pack_start (GTK_BOX (second_hbox), spinbutton2_label, FALSE, FALSE, 3);
  gtk_label_set_justify (GTK_LABEL (spinbutton2_label), GTK_JUSTIFY_RIGHT);

  spinbutton2 = gimp_spin_button_new (&spinbutton_adj2, UserInputValues.lessThan, 
                                     0, 255, 1, 1, 1, 5, 0);
  gtk_box_pack_start (GTK_BOX (second_hbox), spinbutton2, FALSE, FALSE, 0);
  gtk_widget_show (spinbutton2); 

  // Create spin button to set greaterThan variable
  spinbutton3_label = gtk_label_new_with_mnemonic ("_<= x <= m +");
  gtk_widget_show (spinbutton3_label);
  gtk_box_pack_start (GTK_BOX (second_hbox), spinbutton3_label, FALSE, FALSE, 3);
  gtk_label_set_justify (GTK_LABEL (spinbutton3_label), GTK_JUSTIFY_RIGHT);

  spinbutton3 = gimp_spin_button_new (&spinbutton_adj3, UserInputValues.greaterThan, 
                                     0, 255, 1, 1, 1, 5, 0);
  gtk_box_pack_start (GTK_BOX (second_hbox), spinbutton3, FALSE, FALSE, 0);
  gtk_widget_show (spinbutton3);

  spinbutton3_label2 = gtk_label_new_with_mnemonic ("_< x");
  gtk_widget_show (spinbutton3_label2);
  gtk_box_pack_start (GTK_BOX (second_hbox), spinbutton3_label2, FALSE, FALSE, 3);
  gtk_label_set_justify (GTK_LABEL (spinbutton3_label2), GTK_JUSTIFY_RIGHT);

  // Create 2nd checkbox for variant filtering
  button2 = gtk_check_button_new();
  gtk_box_pack_start (GTK_BOX (second_hbox), button2, FALSE, FALSE, 3);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button2), UserInputValues.button2);


  // Add label to the previously created frame2
  frame_label2 = gtk_label_new ("<b>Filtrowanie wariantowe</b>");
  gtk_widget_show (frame_label2);
  gtk_frame_set_label_widget (GTK_FRAME (frame2), frame_label2);
  gtk_label_set_use_markup (GTK_LABEL (frame_label2), TRUE);

  // Adjust dialog accordingly to user input
  g_signal_connect_swapped (preview, "invalidated",
                            G_CALLBACK (median),
                            drawable);
  g_signal_connect_swapped (spinbutton_adj, "value_changed",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);
  g_signal_connect_swapped (spinbutton_adj, "value_changed",
                            G_CALLBACK (gtk_widget_show),
                            hints);
  g_signal_connect_swapped (spinbutton_adj2, "value_changed",
                            G_CALLBACK (gtk_widget_show),
                            button);
  g_signal_connect_swapped (spinbutton_adj3, "value_changed",
                            G_CALLBACK (gtk_widget_show),
                            button2);
  g_signal_connect_swapped (spinbutton_adj2, "value_changed",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);
  g_signal_connect_swapped (spinbutton_adj3, "value_changed",
                            G_CALLBACK (gimp_preview_invalidate),
                            preview);
  gtk_widget_show (dialog);
 
  // Handle parameters updates accordingly to changes in GUI
  g_signal_connect (spinbutton_adj, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &UserInputValues.radius);
  g_signal_connect (button, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &UserInputValues.radius);
  g_signal_connect (button, "clicked",
                    G_CALLBACK(gimp_toggle_button_update), 
                    &UserInputValues.button);
  g_signal_connect (button2, "clicked",
                    G_CALLBACK (gimp_toggle_button_update),
                    &UserInputValues.button2);
  g_signal_connect (spinbutton_adj2, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &UserInputValues.lessThan);
  g_signal_connect (spinbutton_adj3, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &UserInputValues.greaterThan);
  gtk_widget_show (dialog); // Show the entire dialog window

  // Call to median with dialog info
  median (drawable, GIMP_PREVIEW (preview));

  // Keep running until OK button is pressed
  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  // Destroy window
  gtk_widget_destroy (dialog);

  return run;
}
