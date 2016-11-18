
#include <chrono>
#include <cmath>
#include "pie_packet_queue.hh"
#include "timestamp.hh"
#include <unistd.h>
#include <stdio.h>
using namespace std;

#define DQ_COUNT_INVALID   (uint32_t)-1


//Some Hack Here...

double * _drop_prob = NULL;
double rl_drop_prob = 0.0;
unsigned int _size_bytes_queue = 0;
uint32_t * _current_qdelay = NULL;





#define MAXLAYER 5
struct NeuralNetwork{
	int dim_layer[MAXLAYER];
	float **w;
	float **b;	
};


struct NeuralNetwork NN_A,NN_B;

struct NeuralNetwork * NN_cur = NULL; 
pthread_mutex_t swap_lock;




void initNN()
{
	pthread_mutex_init(&swap_lock, NULL);
	//TODO Load Model
	NN_A.dim_layer[0] = 24;
	NN_A.dim_layer[1] = 150;
	NN_A.dim_layer[2] = 200;
	NN_A.dim_layer[3] = 150;
	NN_A.dim_layer[4] = 1;

	NN_B.dim_layer[0] = 24;
	NN_B.dim_layer[1] = 150;
	NN_B.dim_layer[2] = 200;
	NN_B.dim_layer[3] = 150;
	NN_B.dim_layer[4] = 1;

	NN_A.w = (float**)malloc(sizeof(float*) * MAXLAYER);
	NN_B.w = (float**)malloc(sizeof(float*) * MAXLAYER);
	NN_A.b = (float**)malloc(sizeof(float*) * MAXLAYER);
	NN_B.b = (float**)malloc(sizeof(float*) * MAXLAYER);

	for(int i = 0; i< MAXLAYER-1; i++) NN_A.w[i] = (float*)malloc(sizeof(float) * NN_A.dim_layer[i] * NN_A.dim_layer[i+1]);
 	for(int i = 0; i< MAXLAYER-1; i++) NN_B.w[i] = (float*)malloc(sizeof(float) * NN_B.dim_layer[i] * NN_B.dim_layer[i+1]);
 	for(int i = 0; i< MAXLAYER-1; i++) NN_A.b[i] = (float*)malloc(sizeof(float) * NN_A.dim_layer[i+1]);
	for(int i = 0; i< MAXLAYER-1; i++) NN_B.b[i] = (float*)malloc(sizeof(float) * NN_B.dim_layer[i+1]);
 	


	NN_cur = &NN_A;

}


void RunNN(float *input, float* output)
{
	float bufA[256];
	float bufB[256];

	float * pin;
	float * pout;

	pin = input;
	pout = bufA;


	if(NN_cur == NULL) initNN();

	pthread_mutex_lock(&swap_lock);
	
		for(int i = 0; i< MAXLAYER-1; i++)
		{
			
			for(int k=0; k<NN_cur->dim_layer[i+1]; k++)
			{
				pout[k] = NN_cur->b[i][k];
			}

			for(int j=0; j< NN_cur->dim_layer[i]; j++)
			{
				for(int k=0; k<NN_cur->dim_layer[i+1]; k++)
				{
					pout[k] = pout[k] + pin[j] * NN_cur->w[i][j*NN_cur->dim_layer[i+1] + k];
					//if(i==0 && j==0 && k<10) printf("%f ", NN_cur->w[i][j*NN_cur->dim_layer[i+1] + k]);
				}
			}

			if(i != MAXLAYER-2)
				for(int k=0; k<NN_cur->dim_layer[i+1]; k++)
				{
					//printf("%d %f\n",i,pout[k]);
					pout[k] = pout[k] > 0? pout[k]: 0;
					
				}

			
			pin = pout;
			if(pout == bufA) pout = bufB;
			else pout = bufA;

			if(i == MAXLAYER-3) pout = output;
		}

	pthread_mutex_unlock(&swap_lock);

	pout[0] = 1/1.0+exp(-pout[0]);



}


void SwapNN()
{
	pthread_mutex_lock(&swap_lock);

		if(NN_cur == &NN_A) NN_cur = &NN_B;
		else NN_cur = &NN_A;

	pthread_mutex_unlock(&swap_lock);
}



//Update Neure Network Model (Load from file)
void* NN_thread(void* context)
{
	int ret = 0;
	struct NeuralNetwork * NN;
	while(true)
	{
		sleep(1);
		printf("This is NN thread Load \n");

		//TODO Load Parameter
		if(NN_cur == &NN_A)
		{
			NN = &NN_B;
		}
		else
		{
			NN = &NN_A;
		}

		FILE *fp = NULL;
		fp = fopen("/home/songtaohe/Project/QueueManagement/rl-qm/mahimahiInterface/NN.txt","r");
		if(fp == NULL)
		{
			printf("Failed to load parameters\n");
			usleep(1000*10);//10 ms
		}
		else
		{
			printf("Load Parameters\n");
			for(int i=0;i<MAXLAYER-1;i++)
			{
				for(int j=0; j< NN->dim_layer[i] * NN->dim_layer[i+1]; j++) ret += fscanf(fp,"%f",&(NN->w[i][j]));
				for(int j=0; j< NN->dim_layer[i+1]; j++) ret += fscanf(fp,"%f",&(NN->b[i][j]));

			}
			
			fclose(fp);
		}
		
		printf("Swap NN Parameters\n");
		//Swap Buffer
		SwapNN();

	}
	return context;
}


//Update Drop Rate 
void* UpdateDropRate_thread(void* context)
{
	float state[24];
	float action;

	while(true)
	{
		sleep(1);
		for(int i = 0; i<24; i++) state[i] = i;
		RunNN(state, &action);
		rl_drop_prob = action;
		printf("Current Drop Rate %.6lf Queue Size %u Bytes, Qdelay %u  Action %f\n", *_drop_prob, _size_bytes_queue,*_current_qdelay, action);
		
	}
return context;
}




PIEPacketQueue::PIEPacketQueue( const string & args )
  : DroppingPacketQueue(args),
    qdelay_ref_ ( get_arg( args, "qdelay_ref" ) ),
    max_burst_ ( get_arg( args, "max_burst" ) ),
    alpha_ ( 0.125 ),
    beta_ ( 1.25 ),
    t_update_ ( 30 ),
    dq_threshold_ ( 16384 ),
    drop_prob_ ( 0.0 ),
    burst_allowance_ ( 0 ),
    qdelay_old_ ( 0 ),
    current_qdelay_ ( 0 ),
    dq_count_ ( DQ_COUNT_INVALID ),
    dq_tstamp_ ( 0 ),
    avg_dq_rate_ ( 0 ),
    uniform_generator_ ( 0.0, 1.0 ),
    prng_( random_device()() ),
    last_update_( timestamp() ),
		NN_t( 0 ),
		DP_t( 0 )
{
  if ( qdelay_ref_ == 0 || max_burst_ == 0 ) {
    throw runtime_error( "PIE AQM queue must have qdelay_ref and max_burst parameters" );
  }

	
}

void PIEPacketQueue::enqueue( QueuedPacket && p )
{
	static int counter = 0;

	counter++;
	if(counter == 10)
	{
    initNN();
		pthread_create(&(this->NN_t),NULL,&NN_thread,NULL);
		pthread_create(&(this->DP_t),NULL,&UpdateDropRate_thread,NULL);
		printf("Create Pthread!\n");
	}
  calculate_drop_prob();
	
  _drop_prob = &(this->drop_prob_);
	_current_qdelay = &(this->current_qdelay_);
	_size_bytes_queue = size_bytes();

  //printf("%u\n",size_bytes());

  if ( ! good_with( size_bytes() + p.contents.size(),
		    size_packets() + 1 ) ) {
    // Internal queue is full. Packet has to be dropped.
    return;
  } 

  if (!drop_early() ) {
    //This is the negation of the pseudo code in the IETF draft.
    //It is used to enqueue rather than drop the packet
    //All other packets are dropped
    accept( std::move( p ) );
  }

  assert( good() );
}

//returns true if packet should be dropped.
bool PIEPacketQueue::drop_early ()
{
  if ( burst_allowance_ > 0 ) {
    return false;
  }

  if ( qdelay_old_ < qdelay_ref_/2 && drop_prob_ < 0.2) {
    return false;        
  }

  if ( size_bytes() < (2 * PACKET_SIZE) ) {
    return false;
  }

  double random = uniform_generator_(prng_);

  if ( random < drop_prob_ ) {
    return true;
  }
  else
    return false;
}

QueuedPacket PIEPacketQueue::dequeue( void )
{
  QueuedPacket ret = std::move( DroppingPacketQueue::dequeue () );
  uint32_t now = timestamp();

  if ( size_bytes() >= dq_threshold_ && dq_count_ == DQ_COUNT_INVALID ) {
    dq_tstamp_ = now;
    dq_count_ = 0;
  }

  if ( dq_count_ != DQ_COUNT_INVALID ) {
    dq_count_ += ret.contents.size();

    if ( dq_count_ > dq_threshold_ ) {
      uint32_t dtime = now - dq_tstamp_;

      if ( dtime > 0 ) {
	uint32_t rate_sample = dq_count_ / dtime;
	if ( avg_dq_rate_ == 0 ) 
	  avg_dq_rate_ = rate_sample;
	else
	  avg_dq_rate_ = ( avg_dq_rate_ - (avg_dq_rate_ >> 3 )) +
		     (rate_sample >> 3);
                
	if ( size_bytes() < dq_threshold_ ) {
	  dq_count_ = DQ_COUNT_INVALID;
	}
	else {
	  dq_count_ = 0;
	  dq_tstamp_ = now;
	} 

	if ( burst_allowance_ > 0 ) {
	  if ( burst_allowance_ > dtime )
	    burst_allowance_ -= dtime;
	  else
	    burst_allowance_ = 0;
	}
      }
    }
  }

  calculate_drop_prob();

  return ret;
}

void PIEPacketQueue::calculate_drop_prob( void )
{
  uint64_t now = timestamp();
	
  //We can't have a fork inside the mahimahi shell so we simulate
  //the periodic drop probability calculation here by repeating it for the
  //number of periods missed since the last update. 
  //In the interval [last_update_, now] no change occured in queue occupancy 
  //so when this value is used (at enqueue) it will be identical
  //to that of a timer-based drop probability calculation.
  while (now - last_update_ > t_update_) {
    bool update_prob = true;
    qdelay_old_ = current_qdelay_;

    if ( avg_dq_rate_ > 0 ) 
      current_qdelay_ = size_bytes() / avg_dq_rate_;
    else
      current_qdelay_ = 0;

    if ( current_qdelay_ == 0 && size_bytes() != 0 ) {
      update_prob = false;
    }

    double p = (alpha_ * (int)(current_qdelay_ - qdelay_ref_) ) +
      ( beta_ * (int)(current_qdelay_ - qdelay_old_) );

    if ( drop_prob_ < 0.01 ) {
      p /= 128;
    } else if ( drop_prob_ < 0.1 ) {
      p /= 32;
    } else  {
      p /= 16;
    } 

    drop_prob_ += p;

    if ( drop_prob_ < 0 ) {
      drop_prob_ = 0;
    }
    else if ( drop_prob_ > 1 ) {
      drop_prob_ = 1;
      update_prob = false;
    }

        
    if ( current_qdelay_ == 0 && qdelay_old_==0 && update_prob) {
      drop_prob_ *= 0.98;
    }
        
    burst_allowance_ = max( 0, (int) burst_allowance_ -  (int)t_update_ );
    last_update_ += t_update_;

    if ( ( drop_prob_ == 0 )
	 && ( current_qdelay_ < qdelay_ref_/2 ) 
	 && ( qdelay_old_ < qdelay_ref_/2 ) 
	 && ( avg_dq_rate_ > 0 ) ) {
      dq_count_ = DQ_COUNT_INVALID;
      avg_dq_rate_ = 0;
      burst_allowance_ = max_burst_;
    }

  }
}
