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

// create the debug constant to enable/disable debug output
const bool DBG = true;

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
};

// create and initialize the global mutex that controls access to the
// main bitmap data structure, which controls the associated memory
pthread_mutex_t bmpMutex = PTHREAD_MUTEX_INITIALIZER;

// this var holds the number of active, running threads, so main() knows then they are all finished
// it also needs an associated mutex so each thread can access it safely
int runningThreads = 0;
pthread_mutex_t threadCountMutex = PTHREAD_MUTEX_INITIALIZER;

// function declarations
int iteration_to_color( int i, int max );
int iterations_at_point( double x, double y, int max );
void computeImage( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int numThreads );
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
  printf("mandel: x=%lf y=%lf scale=%lf max=%d height=%d width=%d numThreads=%d outfile=%s\n",xcenter,ycenter,scale,max,image_height,image_width,numThreads,outfile);

  // Create a bitmap of the appropriate size.
  struct bitmap *bm = bitmap_create(image_width,image_height);

  // Fill it with a dark blue, for debugging
  bitmap_reset(bm,MAKE_RGBA(0,0,255,0));

  // Compute the Mandelbrot image
  computeImage(bm,xcenter-scale,xcenter+scale,ycenter-scale,ycenter+scale,max,numThreads);

  // if multithreading is used, enter a loop that will continuously check the number
  // of running threads. The loop will not exit untli the counter is zero.
  if( numThreads > 1)
  {
    while(true)
    {
      pthread_mutex_lock(&threadCountMutex);
      if( runningThreads > 0 )
      {
        pthread_mutex_unlock(&threadCountMutex);
        continue;
      }
      else
      {
        pthread_mutex_unlock(&threadCountMutex);
        break;
      }
    }
  }
  

  // Save the image in the stated file.
  if(!bitmap_save(bm,outfile)) {
    fprintf(stderr,"mandel: couldn't write to %s: %s\n",outfile,strerror(errno));
    return 1;
  }

  pthread_mutex_destroy(&bmpMutex);
  pthread_mutex_destroy(&threadCountMutex);
  pthread_exit(NULL);
  exit(EXIT_SUCCESS);
}

/*
Compute an entire Mandelbrot image, writing each point to the given bitmap.
Scale the image to the range (xmin-xmax,ymin-ymax), limiting iterations to "max"
*/

void computeImage( struct bitmap *bm, double xmin, double xmax, double ymin, double ymax, int max, int threadsToUse )
{
  // we are only changing how the image is built with respect to 
  // height, not width. So, the width is constant.
  int width = bitmap_width(bm);
  int totalHeight = bitmap_height(bm);

  if( threadsToUse > 1 )
  {
    // multithreaded

    if(DBG)
    {
      printf("DEBUG: computeImage(): using multithreading..\n");
    }

    // instantiate and allocate memory for the array that will hold all  
    // struct pointers for computeBands() parameters
    struct bandCreationParams * multithreadedArgsArr;
    multithreadedArgsArr = calloc(threadsToUse, sizeof(struct bandCreationParams));

    // check to ensure calloc() was successful by checking for null pointer return
    // exit with failure status if so
    if(multithreadedArgsArr == NULL)
    {
      printf("There was a problem. Please try again.\n");
      
      if(DBG)
      {
        printf("ERROR -> computeImage(): calloc() returned NULL\n");
      }

      exit(EXIT_FAILURE);

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

    // start the loop that spins-off threads
    int i;
    for( i=0 ; i<threadsToUse ; i++ )
    {
      // assign all the threadArgs struct values
      multithreadedArgsArr[i].theBitmap = bm;
      multithreadedArgsArr[i].multithreaded = true;
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
        printf("DEBUG: computeImage(): band %d height bottom bound = %d\n", i, multithreadedArgsArr[i].bandHeightBottom);
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
        printf("DEBUG: computeImage(): band %d height upper bound = %d\n", i, multithreadedArgsArr[i].bandHeightTop);
      }

      pthread_t tid;
      // everything is ready, proceed with creating a thread to do the computation work
      int returnCode = pthread_create( &tid, NULL, computeBands, (void *) &multithreadedArgsArr[i]);

      // check for non-success return code, exit if so
      if( returnCode != 0 )
      {
        printf("There was an issue creating threads, and the program must exit. Please try again.\n");
        if(DBG)
        {
          printf("ERROR -> computeImage(): pthread_create return code = %d: %s.. exiting...\n", returnCode, strerror(returnCode) );
        }
        exit(EXIT_FAILURE);
      }
    } // for
  } // if( threadsToUse > 1 )
  else
  {
    // single-threaded

    if(DBG)
    {
      printf("DEBUG: computeImage(): using single threading..\n");
    }

    struct bandCreationParams singleThreadArgs;

    singleThreadArgs.theBitmap = bm;
    singleThreadArgs.multithreaded = false;
    singleThreadArgs.bandXMin = xmin;
    singleThreadArgs.bandXMax = xmax;
    singleThreadArgs.bandYMin = ymin;
    singleThreadArgs.bandYMax = ymax;
    singleThreadArgs.bandMax = max;
    singleThreadArgs.bandWidth = width;
    singleThreadArgs.bandHeightBottom = 0;
    singleThreadArgs.bandHeightTop = totalHeight;
    singleThreadArgs.bmpTotalHeight = totalHeight;

    computeBands( (void *) &singleThreadArgs );

  } // else

  if(DBG)
  {
    printf("DEBUG: computeImage() exiting..\n");
  }

}

void * computeBands( void * args )
{
  if(DBG)
  {
    printf("DEBUG: computeBands() starting...\n");
  }
  
  //sleep(2);
  // re-cast the struct holding the parameters
  struct bandCreationParams * params = args;

  // save all the params to local variables
  bool multithreading = params->multithreaded;
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

  if ( multithreading )
  {
    if(DBG)
    {
      printf("DEBUG: computeBands(): using multithreading\n");
    }
    
    // increment the count of running threads (global variable), and lock the mutex first
    pthread_mutex_lock(&threadCountMutex);
    runningThreads++;
    pthread_mutex_unlock(&threadCountMutex);

    if(DBG)
    {
      // lock the runningThreads variable and store it in a temp location so it can be 
      // unlocked immediately, then printf'ed
      int runningThreadsIncTemp = 0;
      pthread_mutex_lock(&threadCountMutex);
      runningThreadsIncTemp = runningThreads;
      pthread_mutex_unlock(&threadCountMutex);
      printf("DEBUG: computeBands(): running threads count=%d, current TID=%d\n",runningThreadsIncTemp,(int) pthread_self());
    }
  }
  else
  {
    if(DBG)
    {
      printf("DEBUG: computeBands(): using single threading\n");
    }
  }

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
      // If using multithreading, the lock the mutex first since this call alters the bmp memory, 
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

  if( multithreading )
  {
    
    pthread_mutex_lock(&threadCountMutex);
    runningThreads--;
    pthread_mutex_unlock(&threadCountMutex);
    if(DBG)
    {
      // lock the runningThreads variable and store it in a temp location so it can be
      // unlocked immediately, then printf'ed
      int runningThreadsDecTemp = 0;
      pthread_mutex_lock(&threadCountMutex);
      runningThreadsDecTemp = runningThreads;
      pthread_mutex_unlock(&threadCountMutex);
      printf("DEBUG: computeBands(): running threads count = %d\n",runningThreadsDecTemp);
      printf("DEBUG: computeBands() thread exiting..\n");
    }
    pthread_exit(0);
  }

  if(DBG)
  {
    printf("DEBUG: computeBands() exiting..\n");
  }

  return NULL;
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
