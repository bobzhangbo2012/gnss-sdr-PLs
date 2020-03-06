/*
 * SPDX-FileCopyrightText: (c) 2003, 2004 Lev Walkin <vlm@lionet.info>. All rights reserved.
 * SPDX-License-Identifier: BSD-1-Clause
 * Generated by asn1c-0.9.22 (http://lionet.info/asn1c)
 * From ASN.1 module "ULP-Components"
 *     found in "../supl-common.asn"
 */

#ifndef _CellMeasuredResultsList_H
#define _CellMeasuredResultsList_H

#include <asn_application.h>

/* Including external dependencies */
#include <asn_SEQUENCE_OF.h>
#include <constr_SEQUENCE_OF.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* Forward declarations */
    struct CellMeasuredResults;

    /* CellMeasuredResultsList */
    typedef struct CellMeasuredResultsList
    {
        A_SEQUENCE_OF(struct CellMeasuredResults)
        list;

        /* Context for parsing across buffer boundaries */
        asn_struct_ctx_t _asn_ctx;
    } CellMeasuredResultsList_t;

    /* Implementation */
    extern asn_TYPE_descriptor_t asn_DEF_CellMeasuredResultsList;

#ifdef __cplusplus
}
#endif

/* Referred external types */
#include "CellMeasuredResults.h"

#endif /* _CellMeasuredResultsList_H_ */
#include <asn_internal.h>
