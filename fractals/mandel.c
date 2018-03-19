/*
 * Name: Matt Hamrick
 * ID: 1000433109
 * 
 * Description:
 *  modified mandel program (provided by instructor) that takes an additional
 *  -n parameter indicating how many threads to use to generate the output image
 * 
 * Mandel command for the final image (with 3 total threads):
 * ./mandel -s .000025 -y -1.03265 -m 7000 -x -.163013 -W 600 -H 600 -n 3
 * 
 */

#include "bitmap.h"

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

// create the debug constant to enable/disable debug output
const bool DBG = false;

// this struct holds the arguments that will get passed to the computeBands function
struct threadArgs{
  struct bitmap * theBitmap;
  double threadXMin;
  double threadXMax;
  double threadYMin;
  double threadYMax;
  int threadMax;
  int heightBottom;
  int heightTop;
};

// create and initialize the global mutex that controls access to the
// main bitmap data structure, which controls the associated memory
pthread_mutex_t bmpMutex = PTHREAD_MUTEX_INITIALIZER;

// function declarations
int iteration_to_color( int i, int max );
int iterations_at_point( double x, double y, int max );
void compute_image( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int numThreads );
void computeBands( void * );

void show_help()
{
  printf("Use: mandel [options]\n");
  printf("Where options are:\n");
  printf("-m <max>     The maximum number of iterations per point. (default=1000)\n");
  printf("-x <coord>   X coordinate of image center point. (default=0)\n");
  printf("-y <coord>   Y coordinate of image center point. (default=0)\n");
  printf("-s <scale>   Scale of the image in Mandlebrot coordinates. (default=4)\n");
  printf("-W <pixels>  Width of the image in pixels. (default=500)\n");
  printf("-H <pixels>  Height of the image in pixels. (default=500)\n");
  printf("-n <threads> Number of threads to use to create the image. (default=1)\n");
  printf("-o <file>    Set output file. (default=mandel.bmp)\n");
  printf("-h           Show this help text.\n");
  printf("\nSome examples are:\n");
  printf("mandel -x -0.5 -y -0.5 -s 0.2\n");
  printf("mandel -x -.38 -y -.665 -s .05 -m 100 -n 3\n");
  printf("mandel -x 0.286932 -y 0.014287 -s .0005 -m 1000\n\n");
}

int main( int argc, char *argv[] )
{
  char c;

  // These are the default configuration values used
  // if no command line arguments are given.

  const char *outfile = "mandel.bmp";
  double xcenter = 0;
  double ycenter = 0;
  double scale = 4;
  int image_width = 500;
  int image_height = 500;
  int max = 1000;
  int numThreads = 1;

  // For each command line argument given,
  // override the appropriate configuration value.

  while((c = getopt(argc,argv,"x:y:s:W:H:m:o:n:h"))!=-1) {
    switch(c) {
      case 'x':
        xcenter = atof(optarg);
        break;
      case 'y':
        ycenter = atof(optarg);
        break;
      case 's':
        scale = atof(optarg);
        break;
      case 'W':
        image_width = atoi(optarg);
        break;
      case 'H':
        image_height = atoi(optarg);
        break;
      case 'm':
        max = atoi(optarg);
        break;
      case 'o':
        outfile = optarg;
        break;
      case 'n':
        numThreads = atoi(optarg);
        break;
      case 'h':
        show_help();
        exit(1);
        break;
    }
  }

  if( numThreads < 1 )
  {
    printf("Invalid value for parameter -n, please try again. Please use mandel -h to see the help output.\n");
    exit(EXIT_FAILURE);
  }

  // Display the configuration of the image.
  printf("mandel: x=%lf y=%lf scale=%lf max=%d numThreads=%d outfile=%s\n",xcenter,ycenter,scale,max,numThreads,outfile);

  // Create a bitmap of the appropriate size.
  struct bitmap *bm = bitmap_create(image_width,image_height);

  // Fill it with a dark blue, for debugging
  bitmap_reset(bm,MAKE_RGBA(0,0,255,0));

  // Compute the Mandelbrot image
  compute_image(bm,xcenter-scale,xcenter+scale,ycenter-scale,ycenter+scale,max,numThreads);

  // BUGBUG: Need to ensure the code after compute_image runs ONLY when the image has been fully computed

  // Save the image in the stated file.
  if(!bitmap_save(bm,outfile)) {
    fprintf(stderr,"mandel: couldn't write to %s: %s\n",outfile,strerror(errno));
    return 1;
  }

  return 0;
}

/*
Compute an entire Mandelbrot image, writing each point to the given bitmap.
Scale the image to the range (xmin-xmax,ymin-ymax), limiting iterations to "max"
*/

void compute_image( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int threadsToUse )
{
  int i,j,height;

  // we are only changing how the image is built with respect to 
  // height, not width. So, the width is constant.
  int width = bitmap_width(bm);
  int totalHeight = bitmap_height(bm);

  if( threadsToUse > 1 )
  {
    // instantiate the array that will hold all struct pointers for 
    // computeBands() parameters
    struct threadArgs threadArgsArr[threadsToUse];

    // since the number of threads may not cleanly divide the number of 
    // height pixels, store the modulus of them.
    int modRemainder = totalHeight % threadsToUse;

    // subtract the modulus remainder fromn the overall bmp height 
    // to get a number that will cleanly divide by the number of threads. 
    // The modRemainder will later be re-added to the final thread's 
    // upper-bound height.
    int evenHeight = totalHeight - modRemainder;
    
    // baseHeight will be the main variable to use when determining the bands 
    // of pixels to calculate per thread
    int baseHeight = evenHeight / threadsToUse;

    // start the loop that spins-off threads
    for( i=0 ; i<threadsToUse ; i++ )
    {
      // assign all the threadArgs struct values
      threadArgsArr[i].theBitmap = bm;
      threadArgsArr[i].threadXMin = xmin;
      threadArgsArr[i].threadXMax = xmax;
      threadArgsArr[i].threadYMin = ymin;
      threadArgsArr[i].threadYMax = ymax;
      threadArgsArr[i].threadMax = max;
      
      // calculate the pixels that apply for this iteration of the band
      // the bottom bound is always a multiple of the baseHeight, except when it's zero (first thread)
      threadArgsArr[i].heightBottom = 0 + ( i * baseHeight );

      // the upper bound can be thought of as the lower bound of the next band (the i+1), minus 1
      threadArgsArr[i].heightTop = 0 + ( (i+1) * baseHeight ) - 1 ;

      // except when the current iteration of the loop is for the final thread, in
      // which case we need to add the modRemainder that was calculated earlier
      if( i == threadsToUse - 1 )
      {
        threadArgsArr[i].heightTop = threadArgsArr[i].heightTop + modRemainder ;
      }

      thread_t tid;

      int returnCode = pthread_create( &tid, NULL, computeBands, (void *) &threadArgsArr[i]);

      if( returnCode != 0 )
      {
        printf("There was an issue creating threads, and the program must exit. Please try again.\n");
      }

    } // for
  } // if( threadsToUse > 1 )
  else
  {
    // original, single-threaded calculations
    height = totalHeight;

    for(j=0;j<height;j++) 
    {
      for(i=0;i<width;i++) 
      {
        // Determine the point in x,y space for that pixel.
        double x = xmin + i*(xmax-xmin)/width;
        double y = ymin + j*(ymax-ymin)/height;

        // Compute the iterations at that point.
        int iters = iterations_at_point(x,y,max);

        // Set the pixel in the bitmap.
        bitmap_set(bm,i,j,iters);
      }
    }
  } // else

  // For every pixel in the image...


}

void * computeBands( void * args )
{
  struct threadArgs *params;
  params = (struct threadArgs *) args;

  pthread_exit(NULL);
}

/*
Return the number of iterations at point x, y
in the Mandelbrot space, up to a maximum of max.
*/

int iterations_at_point( double x, double y, int max )
{
  double x0 = x;
  double y0 = y;

  int iter = 0;

  while( (x*x + y*y <= 4) && iter < max ) {

    double xt = x*x - y*y + x0;
    double yt = 2*x*y + y0;

    x = xt;
    y = yt;

    iter++;
  }

  return iteration_to_color(iter,max);
}

/*
Convert an iteration number to an RGBA color.
Here, we just scale to gray with a maximum of imax.
Modify this function to make more interesting colors.
*/

int iteration_to_color( int i, int max )
{
  int gray = 255*i/max;
  return MAKE_RGBA(gray,gray,gray,0);
}




