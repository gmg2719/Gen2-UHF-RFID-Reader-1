/* -*- c++ -*- */
/* 
 * Copyright 2015 <Nikos Kargas (nkargas@isc.tuc.gr)>.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include <gnuradio/prefs.h>
#include <gnuradio/math.h>
#include <cmath>
#include <sys/time.h>
#include "tag_decoder_impl.h"
#include <string>
namespace gr {
  namespace rfid {

    tag_decoder::sptr
      tag_decoder::make(int sample_rate)
      {

        std::vector<int> output_sizes;
        output_sizes.push_back(sizeof(float));
        output_sizes.push_back(sizeof(gr_complex));

        return gnuradio::get_initial_sptr
          (new tag_decoder_impl(sample_rate,output_sizes));
      }

    /*
     * The private constructor
     */
    tag_decoder_impl::tag_decoder_impl(int sample_rate, std::vector<int> output_sizes)
      : gr::block("tag_decoder",
          gr::io_signature::make(1, 1, sizeof(gr_complex)),
          gr::io_signature::makev(2, 2, output_sizes )),
      s_rate(sample_rate)
    {


      char_bits = (char *) malloc( sizeof(char) * 128);

      n_samples_TAG_BIT = TAG_BIT_D * s_rate / pow(10,6);      
      GR_LOG_INFO(d_logger, "Number of samples of Tag bit : "<< n_samples_TAG_BIT);
    }

    /*
     * Our virtual destructor.
     */
    tag_decoder_impl::~tag_decoder_impl()
    {
    }

    void
      tag_decoder_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
      {
        ninput_items_required[0] = noutput_items;
      }



    int tag_decoder_impl::tag_sync(const gr_complex * in , gr_complex * out, int size)
    {
      int max_index = 0;
      float max = 0,corr;
      gr_complex corr2;

      gr_complex sum;
      //float nom = 0.0f;
      sum = std::accumulate(in, in+size, gr_complex()); //add every data of 'in' which is related
      sum = sum / gr_complex(size); //divide to make it avg
      //for(int i = 0; i < size; i++) {
      //  nom += std::norm(in[i] - sum);
      //}
      //nom = sqrt(nom);

      //for (int i=0; i < 6 * n_samples_TAG_BIT ; i++) {
      for (int i=0; i < n_samples_TAG_BIT * (TAG_PREAMBLE_BITS+RN16_BITS) ; i++) {
        corr2 = gr_complex(0, 0);
        corr = 0;
        for (int j = 0; j < 2*TAG_PREAMBLE_BITS; j++) {
          for (int k = 0; k < n_samples_TAG_BIT/2.0; k++) {
            gr_complex sample = in[ (int) (i+j*n_samples_TAG_BIT/2.0 + k) ] - sum;
            corr2 = corr2 + sample * gr_complex(TAG_PREAMBLE[j], 0);
          }
        }
        //corr = std::norm(corr2) / nom;
        corr = corr2.real();
        if (corr > max)
        {
          max = corr;
          max_index = i;
        }
      }
      h_est = sum;
      max_index = max_index + TAG_PREAMBLE_BITS * n_samples_TAG_BIT - n_samples_TAG_BIT/2;
      if(max > 0.01f) 
        return max_index;
      else
        return -max_index;

    }




    std::vector<float>  tag_decoder_impl::tag_detection_RN16(std::vector<gr_complex> & RN16_samples_complex)
    {
      // detection + differential decoder (since Tag uses FM0)
      std::vector<float> tag_bits,dist;
      float masks[4][28] = {
        {-1, -1, -1, -1, -1, -1, -1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 1, 1, 1, 1, 1, 1, 1 },
        {1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1 },
        {1, 1, 1, 1, 1, 1, 1,-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 1, 1, 1, 1, 1, 1},
        {-1, -1, -1, -1, -1, -1, -1,1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1}
      };
      int p, q;

      for (int i = 0; i < RN16_BITS-1; i++) {
        float corr[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        p = (int)(i*n_samples_TAG_BIT);
        q = p + 28;
        for (int j = 0; j < 4; j++) {
          for (int k = p; k < q; k++) {
            corr[j] += masks[j][k-p] * std::real(RN16_samples_complex[k]);
          }
        }

        int maxidx = 0;
        for (int i = 0; i < 4; i++) {
          if (corr[i] > corr[maxidx])
            maxidx = i;
        }

        if (maxidx <= 1)
          tag_bits.push_back(0);
        else
          tag_bits.push_back(1);
      }

      return tag_bits;
    }


    std::vector<float>  tag_decoder_impl::tag_detection_EPC(std::vector<gr_complex> & EPC_samples_complex, int index)
    {
      std::vector<float> tag_bits,dist;
      float result=0;
      int prev = 1;

      int number_steps = 20;
      float min_val = n_samples_TAG_BIT/2.0 -  n_samples_TAG_BIT/2.0/100, max_val = n_samples_TAG_BIT/2.0 +  n_samples_TAG_BIT/2.0/100;

      std::vector<float> energy;

      energy.resize(number_steps);
      for (int t = 0; t <number_steps; t++)
      {  
        for (int i =0; i <256; i++)
        {
          energy[t]+= reader_state->magn_squared_samples[(int) (i * (min_val + t*(max_val-min_val)/(number_steps-1)) + index)];
        }

      }
      int index_T = std::distance(energy.begin(), std::max_element(energy.begin(), energy.end()));
      float T =  min_val + index_T*(max_val-min_val)/(number_steps-1);

      // T estimated
      T_global = T;

      for (int j = 0; j < 128 ; j ++ )
      {
        result = std::real((EPC_samples_complex[ (int) (j*(2*T) + index) ] - EPC_samples_complex[ (int) (j*2*T + T + index) ])*std::conj(h_est) ); 


        if (result>0){
          if (prev == 1)
            tag_bits.push_back(0);
          else
            tag_bits.push_back(1);      
          prev = 1;      
        }
        else
        { 
          if (prev == -1)
            tag_bits.push_back(0);
          else
            tag_bits.push_back(1);      
          prev = -1;    
        }
      }
      return tag_bits;
    }

    std::vector<float> tag_decoder_impl::bit_decoding(
        std::vector<gr_complex> &samples_complex,
        int                     n_expected_bit,
        int                     index)
    {
      std::vector<float> tag_bits;
      

      const float masks[4][4] = {
        {-1, 1, -1, 1},
        {1, -1, 1, -1},
        {1, -1, -1, 1},
        {-1, 1, 1, -1}
      };
      int start, end;

      //decode bit every round
      for (int i = 0; i < n_expected_bit; i++) {
        float corr[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        
        start = (int)(i*n_samples_TAG_BIT);
        end = start + (int)(2*n_samples_TAG_BIT);

        if(i==0)
          std::cout << start<<std::endl;
        else if(i == RN16_BITS)
          std::cout << end << std::endl;

        //calculating correlation values
        for (int j = 0; j < 4; j++) {
          for (int k = start; k < end; k++) {
            int devi = 0;
            int position = k-start;

            if(position<(n_samples_TAG_BIT*0.5))  //first quarter
              devi = 0;
            else if(position<(n_samples_TAG_BIT)) //second quarter
              devi = 1;
            else if(position<(n_samples_TAG_BIT*1.5)) //third quarter
              devi = 2;
            else  //last quarter
              devi = 3; 
            corr[j] += masks[j][devi] * std::real(samples_complex[k]);  //calculate
          }
        }
        int maxidx = 0;

        //find the most maximum correlation values
        for (int i = 0; i < 4; i++) {
          if (corr[i] > corr[maxidx])
            maxidx = i;
        }


        //based on maximum correlation value, decode the tag bits
        if (maxidx <= 1){
          tag_bits.push_back(0);
        }
        else{
          tag_bits.push_back(1);
        }
      }



      return tag_bits;
    }


    int
      tag_decoder_impl::general_work (int noutput_items,
          gr_vector_int &ninput_items,
          gr_vector_const_void_star &input_items,
          gr_vector_void_star &output_items)
      {


        const gr_complex *in = (const  gr_complex *) input_items[0];
        float *out = (float *) output_items[0];
        gr_complex *out_2 = (gr_complex *) output_items[1]; // for debugging

        int written_sync =0;
        int written = 0, consumed = 0;
        int RN16_index , EPC_index;

        std::vector<float> RN16_samples_real;
        std::vector<float> EPC_samples_real;

        std::vector<gr_complex> RN16_samples_complex;
        std::vector<gr_complex> EPC_samples_complex;

        std::vector<float> RN16_bits;
        int number_of_half_bits = 0;


        std::vector<float> EPC_bits;    
        // Processing only after n_samples_to_ungate are available and we need to decode an RN16
        if (reader_state->decoder_status == DECODER_DECODE_RN16 && ninput_items[0] >= reader_state->n_samples_to_ungate)
        {
          RN16_index = tag_sync(in, out_2, ninput_items[0]);  //find where the tag data bits start

          // RN16 bits are passed to the next block for the creation of ACK message
          if ((RN16_index > 0.0f)&&(ninput_items[0]>6000))
          {  
            for(int j = (int)(RN16_index); j < std::min(ninput_items[0]+(int)RN16_index, (int)(RN16_index+(RN16_BITS+2)*n_samples_TAG_BIT)); j++) 
              RN16_samples_complex.push_back(in[j]-h_est);  //subtracting h_est(avg value) and save it in RN16_samples_complex

            preamble_fp = fopen(("decode_data/"+std::to_string(reader_state->reader_stats.cur_inventory_round-1)).c_str(), "w");
            for(int i=0; i < RN16_samples_complex.size(); i++){
              if(i!=0)
                fprintf(preamble_fp, ", ");
              fprintf(preamble_fp, "%f", RN16_samples_complex[i].real());
            }
            fprintf(preamble_fp, "\n");
            fclose(preamble_fp);

            std::vector<float> tag_bits;

            tag_bits = bit_decoding(RN16_samples_complex,RN16_BITS,0);

            char databits[129];

            for(int i = 0; i<128; i++){
              if(tag_bits[i] == 0)
                databits[i] = '0';
              else
                databits[i] = '1';
            }

            databits[128] = NULL;

            std::cout << "data : "<<databits<<std::endl;

            int crc_checked = check_crc(databits, 128);

            if(crc_checked==1)
              std::cout << "Tag decoding Success!"<<std::endl;
            else if(crc_checked==-1)
              std::cout << "Tag decoding Failed!!"<<std::endl;

            //std::cout << "Data : ";

            //for(int i = 0; i<tag_bits.size();i++)
            //{
            //  std::cout<<tag_bits[i];
            //}

            //std::cout<<std::endl;

            RN16_bits = tag_bits;
            for(int bit=0; bit<RN16_bits.size(); bit++)
            {
              out[written] =  RN16_bits[bit];
              written ++;
            }
            produce(0,written);
            reader_state->gen2_logic_status = SEND_ACK;
            reader_state->reader_stats.cur_slot_number++;
            if(reader_state->reader_stats.cur_slot_number > reader_state->reader_stats.max_slot_number)
            {
              reader_state->reader_stats.cur_slot_number = 1;
              reader_state->reader_stats.unique_tags_round.push_back(reader_state->reader_stats.tag_reads.size());

              reader_state->reader_stats.cur_inventory_round += 1;

              reader_state->gen2_logic_status = SEND_QUERY;
            }
            else
            {
              reader_state->gen2_logic_status = SEND_QUERY_REP;
            }

          }
          else
          {  
            reader_state->reader_stats.cur_slot_number++;
            if(reader_state->reader_stats.cur_slot_number > reader_state->reader_stats.max_slot_number)
            {
              reader_state->reader_stats.cur_slot_number = 1;
              reader_state->reader_stats.unique_tags_round.push_back(reader_state->reader_stats.tag_reads.size());

              reader_state->reader_stats.cur_inventory_round += 1;

              //if (P_DOWN == true)
              //  reader_state->gen2_logic_status = POWER_DOWN;
              //else
              reader_state->gen2_logic_status = SEND_QUERY;
            }
            else
            {
              reader_state->gen2_logic_status = SEND_QUERY_REP;
            }
          }
          consumed = reader_state->n_samples_to_ungate;
        }
        else if (reader_state->decoder_status == DECODER_DECODE_EPC && ninput_items[0] >= reader_state->n_samples_to_ungate )
        {  

          //After EPC message send a query rep or query
          reader_state->reader_stats.cur_slot_number++;


          EPC_index = tag_sync(in,out_2, ninput_items[0]);

          for (int j = 0; j < ninput_items[0]; j++ )
          {
            EPC_samples_complex.push_back(in[j]);
          }

          /*
             for (int j = 0; j < ninput_items[0] ; j ++ )
             {
             out_2[written_sync] = in[j];
             written_sync ++;          
             }
             produce(1,written_sync);
             */

          EPC_bits   = tag_detection_EPC(EPC_samples_complex,EPC_index);


          if (EPC_bits.size() == EPC_BITS - 1)
          {
            // float to char -> use Buettner's function
            for (int i =0; i < 128; i ++)
            {
              if (EPC_bits[i] == 0)
                char_bits[i] = '0';
              else
                char_bits[i] = '1';
            }
            if(check_crc(char_bits,128) == 1)
            {

              if(reader_state->reader_stats.cur_slot_number > reader_state->reader_stats.max_slot_number)
              {
                reader_state->reader_stats.cur_slot_number = 1;
                reader_state->reader_stats.unique_tags_round.push_back(reader_state->reader_stats.tag_reads.size());

                reader_state->reader_stats.cur_inventory_round+=1;
                //if (P_DOWN == true)
                //  reader_state->gen2_logic_status = POWER_DOWN;
                //else
                reader_state->gen2_logic_status = SEND_QUERY;
              }
              else
              {
                reader_state->gen2_logic_status = SEND_QUERY_REP;
              }

              reader_state->reader_stats.n_epc_correct+=1;

              int result = 0;
              for(int i = 0 ; i < 8 ; ++i)
              {
                result += std::pow(2,7-i) * EPC_bits[104+i] ;
              }
              GR_LOG_INFO(d_debug_logger, "EPC CORRECTLY DECODED, TAG ID : " << result);

              // Save part of Tag's EPC message (EPC[104:111] in decimal) + number of reads
              std::map<int,int>::iterator it = reader_state->reader_stats.tag_reads.find(result);
              if ( it != reader_state->reader_stats.tag_reads.end())
              {
                it->second ++;
              }
              else
              {
                reader_state->reader_stats.tag_reads[result]=1;
              }
            }
            else
            {     

              if(reader_state->reader_stats.cur_slot_number > reader_state->reader_stats.max_slot_number)
              {
                reader_state->reader_stats.cur_slot_number = 1;
                reader_state->reader_stats.cur_inventory_round+=1;
                //if (P_DOWN == true)
                //  reader_state->gen2_logic_status = POWER_DOWN;
                //else
                //  reader_state->gen2_logic_status = SEND_NAK_Q;
                reader_state->gen2_logic_status = SEND_QUERY;
              }
              else
              {
                //reader_state->gen2_logic_status = SEND_NAK_QR;
                reader_state->gen2_logic_status = SEND_QUERY_REP;
              }


              GR_LOG_INFO(d_debug_logger, "EPC FAIL TO DECODE");  
            }
          }
          else
          {
            GR_LOG_EMERG(d_debug_logger, "CHECK ME");  
          }
          consumed = reader_state->n_samples_to_ungate;
        }
        consume_each(consumed);
        return WORK_CALLED_PRODUCE;
      }


    /* Function adapted from https://www.cgran.org/wiki/Gen2 */
    int tag_decoder_impl::check_crc(char * bits, int num_bits)
    {
      register unsigned short i, j;
      register unsigned short crc_16, rcvd_crc;
      unsigned char * data;
      int num_bytes = num_bits / 8;
      data = (unsigned char* )malloc(num_bytes );
      int mask;

      for(i = 0; i < num_bytes; i++)
      {
        mask = 0x80;
        data[i] = 0;
        for(j = 0; j < 8; j++)
        {
          if (bits[(i * 8) + j] == '1'){
            data[i] = data[i] | mask;
          }
          mask = mask >> 1;
        }
      }
      rcvd_crc = (data[num_bytes - 2] << 8) + data[num_bytes -1];

      crc_16 = 0xFFFF; 
      for (i=0; i < num_bytes - 2; i++)
      {
        crc_16^=data[i] << 8;
        for (j=0;j<8;j++)
        {
          if (crc_16&0x8000)
          {
            crc_16 <<= 1;
            crc_16 ^= 0x1021;
          }
          else
            crc_16 <<= 1;
        }
      }
      crc_16 = ~crc_16;

      free(data);

      if(rcvd_crc != crc_16)
        return -1;
      else
        return 1;
    }
    } /* namespace rfid */
  } /* namespace gr */

