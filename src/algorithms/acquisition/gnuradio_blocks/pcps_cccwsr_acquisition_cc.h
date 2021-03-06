/*!
 * \file pcps_cccwsr_acquisition_cc.h
 * \brief This class implements a Parallel Code Phase Search acquisition
 *  with Coherent Channel Combining With Sign Recovery scheme.
 * \author Marc Molina, 2013. marc.molina.pena(at)gmail.com
 *
 * D.Borio, C.O'Driscoll, G.Lachapelle, "Coherent, Noncoherent and
 * Differentially Coherent Combining Techniques for Acquisition of
 * New Composite GNSS Signals", IEEE Transactions On Aerospace and
 * Electronic Systems vol. 45 no. 3, July 2009, section IV
 *
 * -------------------------------------------------------------------------
 *
 * Copyright (C) 2010-2019  (see AUTHORS file for a list of contributors)
 *
 * GNSS-SDR is a software defined Global Navigation
 *          Satellite Systems receiver
 *
 * This file is part of GNSS-SDR.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * -------------------------------------------------------------------------
 */

#ifndef GNSS_SDR_PCPS_CCCWSR_ACQUISITION_CC_H
#define GNSS_SDR_PCPS_CCCWSR_ACQUISITION_CC_H

#include "channel_fsm.h"
#include "gnss_synchro.h"
#include <gnuradio/block.h>
#include <gnuradio/fft/fft.h>
#include <gnuradio/gr_complex.h>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#if GNURADIO_USES_STD_POINTERS
#else
#include <boost/shared_ptr.hpp>
#endif


class pcps_cccwsr_acquisition_cc;

#if GNURADIO_USES_STD_POINTERS
using pcps_cccwsr_acquisition_cc_sptr = std::shared_ptr<pcps_cccwsr_acquisition_cc>;
#else
using pcps_cccwsr_acquisition_cc_sptr = boost::shared_ptr<pcps_cccwsr_acquisition_cc>;
#endif

pcps_cccwsr_acquisition_cc_sptr pcps_cccwsr_make_acquisition_cc(
    uint32_t sampled_ms,
    uint32_t max_dwells,
    uint32_t doppler_max,
    int64_t fs_in,
    int32_t samples_per_ms,
    int32_t samples_per_code,
    bool dump,
    const std::string& dump_filename);

/*!
 * \brief This class implements a Parallel Code Phase Search Acquisition with
 * Coherent Channel Combining With Sign Recovery scheme.
 */
class pcps_cccwsr_acquisition_cc : public gr::block
{
public:
    /*!
     * \brief Default destructor.
     */
    ~pcps_cccwsr_acquisition_cc();

    /*!
     * \brief Set acquisition/tracking common Gnss_Synchro object pointer
     * to exchange synchronization data between acquisition and tracking blocks.
     * \param p_gnss_synchro Satellite information shared by the processing blocks.
     */
    inline void set_gnss_synchro(Gnss_Synchro* p_gnss_synchro)
    {
        d_gnss_synchro = p_gnss_synchro;
    }

    /*!
     * \brief Returns the maximum peak of grid search.
     */
    inline uint32_t mag() const
    {
        return d_mag;
    }

    /*!
     * \brief Initializes acquisition algorithm.
     */
    void init();

    /*!
     * \brief Sets local code for CCCWSR acquisition algorithm.
     * \param data_code - Pointer to the data PRN code.
     * \param pilot_code - Pointer to the pilot PRN code.
     */
    void set_local_code(std::complex<float>* code_data, std::complex<float>* code_pilot);

    /*!
     * \brief Starts acquisition algorithm, turning from standby mode to
     * active mode
     * \param active - bool that activates/deactivates the block.
     */
    inline void set_active(bool active)
    {
        d_active = active;
    }

    /*!
     * \brief If set to 1, ensures that acquisition starts at the
     * first available sample.
     * \param state - int=1 forces start of acquisition
     */
    void set_state(int32_t state);

    /*!
     * \brief Set acquisition channel unique ID
     * \param channel - receiver channel.
     */
    inline void set_channel(uint32_t channel)
    {
        d_channel = channel;
    }

    /*!
     * \brief Set channel fsm associated to this acquisition instance
     */
    inline void set_channel_fsm(std::weak_ptr<ChannelFsm> channel_fsm)
    {
        d_channel_fsm = std::move(channel_fsm);
    }

    /*!
     * \brief Set statistics threshold of CCCWSR algorithm.
     * \param threshold - Threshold for signal detection (check \ref Navitec2012,
     * Algorithm 1, for a definition of this threshold).
     */
    inline void set_threshold(float threshold)
    {
        d_threshold = threshold;
    }

    /*!
     * \brief Set maximum Doppler grid search
     * \param doppler_max - Maximum Doppler shift considered in the grid search [Hz].
     */
    inline void set_doppler_max(uint32_t doppler_max)
    {
        d_doppler_max = doppler_max;
    }

    /*!
     * \brief Set Doppler steps for the grid search
     * \param doppler_step - Frequency bin of the search grid [Hz].
     */
    inline void set_doppler_step(uint32_t doppler_step)
    {
        d_doppler_step = doppler_step;
    }

    /*!
     * \brief Coherent Channel Combining With Sign Recovery Acquisition signal processing.
     */
    int general_work(int noutput_items, gr_vector_int& ninput_items,
        gr_vector_const_void_star& input_items,
        gr_vector_void_star& output_items);

private:
    friend pcps_cccwsr_acquisition_cc_sptr
    pcps_cccwsr_make_acquisition_cc(uint32_t sampled_ms, uint32_t max_dwells,
        uint32_t doppler_max, int64_t fs_in,
        int32_t samples_per_ms, int32_t samples_per_code,
        bool dump, const std::string& dump_filename);

    pcps_cccwsr_acquisition_cc(uint32_t sampled_ms, uint32_t max_dwells,
        uint32_t doppler_max, int64_t fs_in,
        int32_t samples_per_ms, int32_t samples_per_code,
        bool dump, const std::string& dump_filename);

    void calculate_magnitudes(gr_complex* fft_begin, int32_t doppler_shift,
        int32_t doppler_offset);

    int64_t d_fs_in;
    int32_t d_samples_per_ms;
    int32_t d_samples_per_code;
    uint32_t d_doppler_resolution;
    float d_threshold;
    std::string d_satellite_str;
    uint32_t d_doppler_max;
    uint32_t d_doppler_step;
    uint32_t d_sampled_ms;
    uint32_t d_max_dwells;
    uint32_t d_well_count;
    uint32_t d_fft_size;
    uint64_t d_sample_counter;
    std::vector<std::vector<gr_complex>> d_grid_doppler_wipeoffs;
    uint32_t d_num_doppler_bins;
    std::vector<gr_complex> d_fft_code_data;
    std::vector<gr_complex> d_fft_code_pilot;
    std::shared_ptr<gr::fft::fft_complex> d_fft_if;
    std::shared_ptr<gr::fft::fft_complex> d_ifft;
    Gnss_Synchro* d_gnss_synchro;
    uint32_t d_code_phase;
    float d_doppler_freq;
    float d_mag;
    std::vector<float> d_magnitude;
    std::vector<gr_complex> d_data_correlation;
    std::vector<gr_complex> d_pilot_correlation;
    std::vector<gr_complex> d_correlation_plus;
    std::vector<gr_complex> d_correlation_minus;
    float d_input_power;
    float d_test_statistics;
    std::ofstream d_dump_file;
    bool d_active;
    int32_t d_state;
    bool d_dump;
    uint32_t d_channel;
    std::weak_ptr<ChannelFsm> d_channel_fsm;
    std::string d_dump_filename;
};

#endif  // GNSS_SDR_PCPS_CCCWSR_ACQUISITION_CC_H
