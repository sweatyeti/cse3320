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
#include <unistd.h>
#include <sys/time.h>

// enable/disable debug output
bool DBG = false;

// enable/disable timing output
bool TIMING = false;

// this struct holds the arguments that will get passed to the computeBands function
struct bandCreationParams{
  struct bitmap * theBitmap;
  double bandXMin;
  double bandXMax;
  double bandYMin;
  double bandYMax;
  int bandMax;
  int bandWidth;
  int bmpTotalHeight;
  int bandHeightBottom;
  int bandHeightTop;
  bool multithreaded;
  int tid; 
};

// create and initialize the global mutex that controls access to the
// main bitmap data structure, which controls the associated memory
pthread_mutex_t bmpMutex = PTHREAD_MUTEX_INITIALIZER;

// function declarations
static int iteration_to_color( int i, int max );
static int iterations_at_point( double x, double y, int max );
bool computeImage( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int numThreads );
void * computeBands( void * );

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
  // declare the vars that hold time values, just in case timing has been enabled
  struct timeval computeStart;
  struct timeval computeEnd;

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
  char c;
  while((c = getopt(argc,argv,"x:y:s:W:H:m:o:n:hdt"))!=-1) {
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
      case 'd':
        DBG = true;
        break;
      case 't':
        TIMING = true;
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
  printf("mandel: x=%lf y=%lf scale=%lf max=%d height=%d width=%d numThreads=%d outfile=%s\n",xcenter,ycenter,scale,max,image_height,image_width,numThreads,outfile);

  // Create a bitmap of the appropriate size.
  struct bitmap *bm = bitmap_create(image_width,image_height);

  // Fill it with green, for debugging
  bitmap_reset(bm,MAKE_RGBA(0,255,0,0));

  // if this is being timed, get the time value before computation and store it
  if(TIMING)
  {
    gettimeofday( &computeStart, NULL );
  }

  // Compute the Mandelbrot image - this is where all the action happens
  // it returns a bool depending on whether or not it was successful
  bool imageComputed = false;
  imageComputed = computeImage(bm,xcenter-scale,xcenter+scale,ycenter-scale,ycenter+scale,max,numThreads);

  if( !imageComputed )
  {
    printf("There was a problem. Please try again.\n");
    if(DBG)
    {
      printf("ERROR -> main(): computeImage() returned false, no image created...\n");
    }
    exit(EXIT_FAILURE);
  }

  // if this is being timed, get the time value after computation and store it
  if(TIMING)
  {
    gettimeofday( &computeEnd, NULL );
  }

  // Save the image in the stated file.
  if(!bitmap_save(bm,outfile)) {
    fprintf(stderr,"mandel: couldn't write to %s: %s\n",outfile,strerror(errno));
    exit(EXIT_FAILURE);
  }

  // if this is being timed, calculate & output the time taken in microseconds to run the computation
  if(TIMING)
  {
    int computationTime = ( ( computeEnd.tv_sec - computeStart.tv_sec ) * 1000000 + ( computeEnd.tv_usec - computeStart.tv_usec ) );
    printf( "mandel: Computed time taken (in usec): %d\n", computationTime );
  }

  if(DBG)
  {
    printf("DEBUG: main() exiting...\n");
  }
  exit(EXIT_SUCCESS);
}

/*
Compute an entire Mandelbrot image, writing each point to the given bitmap.
Scale the image to the range (xmin-xmax,ymin-ymax), limiting iterations to "max"
*/

bool computeImage( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int threadsToUse )
{
  if(DBG)
  {
    printf("DEBUG: computeImage() starting...\n");
  }
  // we are only changing how the image is built with respect to 
  // height, not width. So, the width is constant.
  int width = bitmap_width(bm);
  int totalHeight = bitmap_height(bm);

  // declare a pointer var to hold all thread IDs just in case multithreading is used (memory will get allocated later)
  pthread_t * threadsArr;

  if( threadsToUse > 1 )
  {
    // multithreaded

    if(DBG)
    {
      printf("DEBUG: computeImage(): using multithreading with %d threads..\n", threadsToUse);
    }

    // instantiate and allocate memory for the array that will hold all  
    // struct pointers for computeBands() parameters
    struct bandCreationParams * multithreadedArgsArr;
    multithreadedArgsArr = (struct bandCreationParams *) calloc( threadsToUse, sizeof(struct bandCreationParams) );

    // check to ensure calloc() was successful by checking for null pointer return
    // return to main if it failed
    if(multithreadedArgsArr == NULL)
    {
      if(DBG)
      {
        printf("ERROR -> computeImage(): calloc() for multithreadedArgsArr returned NULL\n");
      }
      return false;
    }

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

    // allocate memory for the array that holds all the thread IDs,
    // then check to make sure we got it. if not, return to main
    threadsArr = (pthread_t *) calloc( threadsToUse, sizeof(pthread_t) );
    if( threadsArr == NULL)
    {
      if(DBG)
      {
        printf("ERROR -> computeImage(): calloc() for threadsArr returned NULL\n");
      }
      return false;
    }

    // start the loop that spins-off threads
    int i;
    for( i=0 ; i<threadsToUse ; i++ )
    {
      // assign all the threadArgs struct values
      multithreadedArgsArr[i].theBitmap = bm;
      multithreadedArgsArr[i].multithreaded = true;
      multithreadedArgsArr[i].tid = i;
      multithreadedArgsArr[i].bandXMin = xmin;
      multithreadedArgsArr[i].bandXMax = xmax;
      multithreadedArgsArr[i].bandYMin = ymin;
      multithreadedArgsArr[i].bandYMax = ymax;
      multithreadedArgsArr[i].bandMax = max;
      multithreadedArgsArr[i].bandWidth = width;
      multithreadedArgsArr[i].bmpTotalHeight = totalHeight;
      
      // calculate the pixels that apply for this iteration of the band
      // the bottom bound is always a multiple of the baseHeight, except when it's zero (first thread)
      multithreadedArgsArr[i].bandHeightBottom = 0 + ( i * baseHeight );
      if(DBG)
      {
        printf( "DEBUG: computeImage(): band/thread %d height bottom bound = %d\n", i, multithreadedArgsArr[i].bandHeightBottom );
      }

      // the upper bound can be thought of as the lower bound of the next band (the i+1), minus 1 ...
      multithreadedArgsArr[i].bandHeightTop = 0 + ( (i+1) * baseHeight ) - 1;

      // ... except when the current iteration of the loop is for the final thread, in
      // which case we need to add the modRemainder that was calculated earlier
      if( i == threadsToUse - 1 )
      {
        multithreadedArgsArr[i].bandHeightTop += modRemainder;
      }

      if(DBG)
      {
        printf( "DEBUG: computeImage(): band/thread %d height upper bound = %d\n", i, multithreadedArgsArr[i].bandHeightTop );
      }

      // everything is ready, proceed with creating a thread to do the computation work.
      // store the TID in the threadsArr array for later joining
      int returnCode = pthread_create( &threadsArr[i], NULL, computeBands, (void *) &multithreadedArgsArr[i]);

      // check for non-success return code, alert the user and return to main() if so
      if( returnCode != 0 )
      {
        printf("There was an issue creating threads, and the program must exit.\n");
        printf("This is often a transient error, so it will most likely work without issue when retrying.\n");
        if(DBG)
        {
          printf( "ERROR -> computeImage(): pthread_create return code = %d: %s.. exiting...\n", returnCode, strerror(returnCode) );
        }
        return false;
      }
    } // for

    // all threads have been created, or asked to be created, so wait on them before returning
    int k;
    int joinResult;
    for( k=0 ; k<threadsToUse ; k++ )
    {
      joinResult = pthread_join( threadsArr[k], NULL );
      if( DBG && joinResult != 0 )
      {
        printf( "ERROR -> computeImage(): pthread_join returned error %d: %s...\n", joinResult, strerror(joinResult) );
      }

      if(DBG)
      {
        printf( "DEBUG: computeImage(): thread %d exited... \n", k );
      }

    }
    // release the threadsArr array memory since we're finished with it
    free(threadsArr);

  } // if( threadsToUse > 1 )
  else
  {
    // single-threaded

    if(DBG)
    {
      printf("DEBUG: computeImage(): using single threading..\n");
    }

    // declare and initialize the struct and its fields, to be passed to computeBands
    // nothing fancy here since we're not using multithreading 
    struct bandCreationParams singleThreadArgs;

    singleThreadArgs.theBitmap = bm;
    singleThreadArgs.multithreaded = false;
    singleThreadArgs.tid = 0;
    singleThreadArgs.bandXMin = xmin;
    singleThreadArgs.bandXMax = xmax;
    singleThreadArgs.bandYMin = ymin;
    singleThreadArgs.bandYMax = ymax;
    singleThreadArgs.bandMax = max;
    singleThreadArgs.bandWidth = width;
    singleThreadArgs.bandHeightBottom = 0;
    singleThreadArgs.bmpTotalHeight = totalHeight;
    // computeBands() expects bandHeightTop to be the top of the image based on a zero index (i.e. 0-499 instead of 1-500),
    // so take our totalHeight and subtract one so the amount is correct
    singleThreadArgs.bandHeightTop = totalHeight-1;
    

    // since the same computeBands is used in both single and multithreading scenarios, 
    // need to pass the address of the struct to it. This is so another method just for 
    // multithreading didn't need to be created.
    computeBands( (void *) &singleThreadArgs );

  } // else

  // we no longer need the mutex that locks the bmp memory, destroy it
  pthread_mutex_destroy(&bmpMutex);

  if(DBG)
  {
    printf("DEBUG: computeImage() exiting..\n");
  }

  return true;
} // computeImage()

void * computeBands( void * args )
{
  if(DBG)
  {
    printf("DEBUG: computeBands() starting with ");
  }

  // re-cast the struct holding the parameters
  struct bandCreationParams * params = args;

  // save all the params to local variables
  bool multithreading = params->multithreaded;
  int threadId = params->tid;
  struct bitmap * bm = params->theBitmap;
  double xmin = params->bandXMin;
  double xmax = params->bandXMax;
  double ymin = params->bandYMin;
  double ymax = params->bandYMax;
  int max = params->bandMax;
  int width = params->bandWidth;
  int totalHeight = params->bmpTotalHeight;
  int heightLowerBound = params->bandHeightBottom;
  int heightUpperBound = params->bandHeightTop;

  if(DBG)
  {
    if ( multithreading )
    {
      printf( "multithreading; current TID=%d\n", threadId );
    }
    else
    {
      printf( "single threading\n" );
    }
  }
  
  // declare counters for the for loops below
  int i,j;

  // For every pixel in the image...
  for( j=heightLowerBound ; j<=heightUpperBound ; j++) 
  {
    for( i=0 ; i<width ; i++) 
    {
      // Determine the point in x,y space for that pixel.
      double x = xmin + i*(xmax-xmin)/width;
      double y = ymin + j*(ymax-ymin)/totalHeight;

      // Compute the iterations at that point.
      int iters = iterations_at_point(x,y,max);

      // Set the pixel in the bitmap.
      // If using multithreading, lock the mutex first since this call alters the global bmp memory, 
      // which is shared amongst the threads.
      if( multithreading )
      {
        pthread_mutex_lock(&bmpMutex);
        bitmap_set(bm,i,j,iters);
        pthread_mutex_unlock(&bmpMutex);
      }
      else
      {
        bitmap_set(bm,i,j,iters);
      }
      
    } // inner for
  } // outer for

  // the calculation is finished at this point, so exit the thread if using multithreading
  if( multithreading )
  {
    if(DBG)
    {
      printf( "DEBUG: computeBands() thread %d: exiting..\n", threadId );
    }
    pthread_exit(NULL);
  }

  if(DBG)
  {
    printf( "DEBUG: computeBands() exiting..\n" );
  }

  // only singlethreading scenarios get to this point, so return the null ptr
  return NULL;
}

/*
Return the number of iterations at point x, y
in the Mandelbrot space, up to a maximum of max.
*/

static int iterations_at_point( double x, double y, int max )
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

static int iteration_to_color( int i, int max )
{
  int gray = 255*i/max;
  return MAKE_RGBA(gray,gray,gray,0);
}
