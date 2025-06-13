/****************************************************************************
 *
 * afgsub.c
 *
 *   Auto-fitter routines to parse the GSUB table (body).
 *
 * Copyright (C) 2025 by
 * David Turner, Robert Wilhelm, and Werner Lemberg.
 *
 * This file is part of the FreeType project, and may only be used,
 * modified, and distributed under the terms of the FreeType project
 * license, LICENSE.TXT.  By continuing to use, modify, or distribute
 * this file you indicate that you have read the license and
 * understand and accept it fully.
 *
 */

#include <freetype/freetype.h>
#include <freetype/tttags.h>

#include <freetype/internal/ftstream.h>

#include "aftypes.h"


#ifdef FT_CONFIG_OPTION_USE_HARFBUZZ


  /*********************************/
  /********                 ********/
  /******** GSUB validation ********/
  /********                 ********/
  /*********************************/


  static FT_Bool
  af_validate_coverage( FT_Byte*  table,
                        FT_ULong  max_table_length,
                        FT_UInt  *num_glyphs )
  {
    FT_UInt   format;
    FT_Byte*  p;


    *num_glyphs = 0;

    /* The two bytes for `format` are already checked. */
    /* Higher byte is always zero. */
    format = table[1];

    p = table + 2;

    if ( format == 1 )
    {
      FT_UInt  glyphCount;


      /* The two bytes for `glyphCount` are already checked. */
      glyphCount  = FT_PEEK_USHORT( p );
      /* We don't validate glyph IDs. */
      if ( max_table_length < 4 + glyphCount * 2 )
        return FALSE;

      *num_glyphs += glyphCount;
    }
    else if ( format == 2 )
    {
      FT_UInt   rangeCount;
      FT_Byte*  limit;


      /* The two bytes for `rangeCount` are already checked. */
      rangeCount = FT_NEXT_USHORT( p );
      if ( max_table_length < 4 + rangeCount * 6 )
        return FALSE;

      limit = p + rangeCount * 6;
      while ( p < limit )
      {
        FT_UInt  startGlyphID;
        FT_UInt  endGlyphID;


        startGlyphID = FT_NEXT_USHORT( p );
        endGlyphID   = FT_NEXT_USHORT( p );

        if ( startGlyphID > endGlyphID )
          return FALSE;

        *num_glyphs += endGlyphID - startGlyphID + 1;

        /* We don't validate coverage indices. */
        p += 2;
      }
    }
    else
      return FALSE;

    return TRUE;
  }


  static FT_Bool
  af_validate_single_subst1( FT_Byte*  table,
                             FT_ULong  max_table_length )
  {
    FT_UInt  coverageOffset;
    FT_UInt  num_glyphs;


    /* The four bytes for `coverageOffset` and `deltaGlyphID` */
    /* are already checked.                                   */
    coverageOffset = FT_PEEK_USHORT( table + 2 );

    /* Ensure the first four bytes of all coverage table formats. */
    if ( max_table_length < coverageOffset + 4 )
      return FALSE;

    if ( !af_validate_coverage( table + coverageOffset,
                                max_table_length - coverageOffset,
                                &num_glyphs ) )
      return FALSE;

    /* We don't validate glyph IDs. */

    return TRUE;
  }


  static FT_Bool
  af_validate_single_subst2( FT_Byte*  table,
                             FT_ULong  max_table_length )
  {
    FT_UInt  coverageOffset;
    FT_UInt  glyphCount;
    FT_UInt  num_glyphs;

    FT_Byte*  p = table + 2;


    /* The four bytes for `coverageOffset` and `glyphCount` */
    /* are already checked.                                 */
    coverageOffset = FT_NEXT_USHORT( p );

    /* Ensure the first four bytes of all coverage table formats. */
    if ( max_table_length < coverageOffset + 4 )
      return FALSE;

    if ( !af_validate_coverage( table + coverageOffset,
                                max_table_length - coverageOffset,
                                &num_glyphs ) )
      return FALSE;

    glyphCount = FT_NEXT_USHORT( p );

    /* We don't validate glyph IDs. */
    if ( max_table_length < 6 + glyphCount * 2 )
      return FALSE;

    if ( glyphCount != num_glyphs )
      return FALSE;

    return TRUE;
  }


  static FT_Bool
  af_validate_alternate( FT_Byte*  table,
                         FT_ULong  max_table_length )
  {
    FT_UInt  coverageOffset;
    FT_UInt  alternateSetCount;
    FT_UInt  num_glyphs;

    FT_Byte*  p = table + 2;
    FT_Byte*  limit;


    /* The four bytes for `coverageOffset` and `alternateSetCount` */
    /* are already checked.                                        */
    coverageOffset = FT_NEXT_USHORT( p );

    /* Ensure the first four bytes of all coverage table formats. */
    if ( max_table_length < coverageOffset + 4 )
      return FALSE;

    if ( !af_validate_coverage( table + coverageOffset,
                                max_table_length - coverageOffset,
                                &num_glyphs ) )
      return FALSE;

    alternateSetCount = FT_NEXT_USHORT( p );
    if ( max_table_length < 6 + alternateSetCount * 2 )
      return FALSE;

    if ( alternateSetCount != num_glyphs )
      return FALSE;

    limit = p + alternateSetCount * 2;
    while ( p < limit )
    {
      FT_UInt  alternateSetOffset;
      FT_UInt  glyphCount;


      alternateSetOffset = FT_NEXT_USHORT( p );
      if ( max_table_length < alternateSetOffset + 2 )
        return FALSE;

      glyphCount = FT_PEEK_USHORT( table + alternateSetOffset );

      /* We don't validate glyph IDs. */
      if ( max_table_length < alternateSetOffset + 2 + glyphCount * 2 )
        return FALSE;
    }

    return TRUE;
  }


  static FT_Bool
  af_validate_lookup_table( FT_Byte*  table,
                            FT_ULong  max_table_length )
  {
    FT_UInt  lookupType;
    FT_UInt  real_lookupType = 0xFFFF;
    FT_UInt  subtableCount;

    FT_Byte*  p;
    FT_Byte*  limit;


    if ( max_table_length < 6 )
      return FALSE;

    /* Higher byte is always zero. */
    lookupType = table[1];

    p = table + 4;

    subtableCount = FT_NEXT_USHORT( p );
    if ( max_table_length < 6 + subtableCount * 2 )
      return FALSE;

    limit = p + subtableCount * 2;
    while ( p < limit )
    {
      FT_UInt  subtableOffset;
      FT_UInt  format;

      FT_Byte*  subtable;
      FT_ULong  max_subtable_length;


      subtableOffset = FT_NEXT_USHORT( p );

      if ( lookupType == 7 )
      {
        /* Substitution extension format. */
        FT_UInt  offset;


        if ( max_table_length < subtableOffset + 8 )
          return FALSE;

        /* Higher byte is always zero. */
        if ( real_lookupType == 0xFFFF )
          real_lookupType = table[subtableOffset + 3];
        else if ( real_lookupType != table[subtableOffset + 3] )
          return FALSE;

        offset          = FT_PEEK_LONG( table + subtableOffset + 4 );
        subtableOffset += offset;
      }
      else
        real_lookupType = lookupType;

      /* Ensure the first six bytes of all subtable formats. */
      if ( max_table_length < subtableOffset + 6 )
        return FALSE;

      /* Higher byte is always zero. */
      format = table[subtableOffset + 1];

      subtable            = table + subtableOffset;
      max_subtable_length = max_table_length - subtableOffset;

      if ( real_lookupType == 1 )
      {
        if ( format == 1 )
        {
          if ( !af_validate_single_subst1( subtable, max_subtable_length ) )
            return FALSE;
        }
        else if ( format == 2 )
        {
          if ( !af_validate_single_subst2( subtable, max_subtable_length ) )
            return FALSE;
        }
        else
          return FALSE;
      }
      else if ( real_lookupType == 3 )
      {
        if ( format == 1 )
        {
          if ( !af_validate_alternate( subtable, max_subtable_length ) )
            return FALSE;
        }
        else
          return FALSE;
      }
      else
        return FALSE;
    }

    return TRUE;
  }


  FT_LOCAL_DEF( void )
  af_parse_gsub( AF_FaceGlobals  globals )
  {
    FT_Error  error = FT_Err_Ok;

    FT_Face    face   = globals->face;
    FT_Memory  memory = face->memory;

    FT_ULong  gsub_length;
    FT_Byte*  gsub;

    FT_UInt32*  gsub_lookups_single_alternate;

    FT_UInt   lookupListOffset;
    FT_Byte*  lookupList;
    FT_UInt   lookupCount;

    FT_UInt  idx;

    FT_Byte*  p;
    FT_Byte*  limit;


    globals->gsub_length = 0;
    globals->gsub        = NULL;

    globals->gsub_lookups_single_alternate = NULL;

    if ( !hb( version_atleast )( 7, 2, 0 ) )
      return;

    /* No error if we can't load and parse GSUB data. */

    gsub                          = NULL;
    gsub_lookups_single_alternate = NULL;

    gsub_length = 0;
    if ( FT_Load_Sfnt_Table( face, TTAG_GSUB, 0, NULL, &gsub_length ) )
      goto Fail;

    if ( FT_QALLOC( gsub, gsub_length ) )
      goto Fail;

    if ( FT_Load_Sfnt_Table( face, TTAG_GSUB, 0, gsub, &gsub_length ) )
      goto Fail;

    if ( gsub_length < 10 )
      goto Fail;

    lookupListOffset = FT_PEEK_USHORT( gsub + 8 );
    if ( gsub_length < lookupListOffset + 2 )
      goto Fail;

    lookupCount = FT_PEEK_USHORT( gsub + lookupListOffset );
    if ( gsub_length < lookupListOffset + 2 + lookupCount * 2 )
      goto Fail;

    if ( FT_NEW_ARRAY( gsub_lookups_single_alternate, lookupCount ) )
      goto Fail;

    lookupList = gsub + lookupListOffset;
    p          = lookupList + 2;
    limit      = p + lookupCount * 2;
    idx        = 0;
    while ( p < limit )
    {
      FT_UInt32  lookupOffset;
      FT_UInt    lookupType;


      lookupOffset  = FT_NEXT_USHORT( p );
      lookupOffset += lookupListOffset;
      if ( gsub_length < lookupOffset + 2 )
        goto Next;

      /* Higher byte is always zero. */
      lookupType = gsub[lookupOffset + 1];
      if ( lookupType == 7 )
      {
        /* Substitution extension format. */
        FT_UInt  subtableOffset0;
        FT_UInt  offset;


        offset = lookupOffset + 6;
        if ( gsub_length < offset + 2 )
          goto Next;

        subtableOffset0  = FT_PEEK_USHORT( gsub + offset );
        subtableOffset0 += lookupOffset;
        if ( gsub_length < subtableOffset0 + 4 )
          goto Next;

        /* Higher byte is always zero. */
        lookupType = gsub[subtableOffset0 + 3];
      }

      if ( !( lookupType == 1 || lookupType == 3 ) )
        goto Next;

      if ( !af_validate_lookup_table( gsub + lookupOffset,
                                      gsub_length - lookupOffset ) )
        goto Next;

      gsub_lookups_single_alternate[idx] = lookupOffset;

    Next:
      idx++;
    }

    globals->gsub_length = gsub_length;
    globals->gsub        = gsub;

    globals->gsub_lookups_single_alternate = gsub_lookups_single_alternate;

    return;

  Fail:
    FT_FREE( gsub );
    FT_FREE( gsub_lookups_single_alternate );
  }


  /*********************************/
  /********                 ********/
  /********   GSUB access   ********/
  /********                 ********/
  /*********************************/


  static FT_UInt
  af_coverage_format( FT_Byte*  coverage )
  {
    return coverage[1];
  }


  static FT_Byte*
  af_coverage_start( FT_Byte*  coverage )
  {
    return coverage + 4;
  }


  static FT_Byte*
  af_coverage_limit( FT_Byte*  coverage )
  {
    if ( af_coverage_format( coverage ) == 1 )
    {
      FT_UInt  glyphCount = FT_PEEK_USHORT( coverage + 2 );


      return af_coverage_start( coverage ) + glyphCount * 2;
    }
    else
    {
      FT_UInt  rangeCount = FT_PEEK_USHORT( coverage + 2 );


      return af_coverage_start( coverage ) + rangeCount * 6;
    }
  }


  typedef struct AF_CoverageIteratorRec_*  AF_CoverageIterator;

  typedef struct  AF_CoverageIteratorRec_
  {
    FT_UInt  format;

    FT_Byte*  p;           /* to be set to start before iterating */
    FT_Byte*  limit;       /* to be set before iterating          */

    FT_UInt  glyph;        /* to be set before iterating to a value ... */
    FT_UInt  glyph_limit;  /* ... larger than this one                  */

  } AF_CoverageIteratorRec;


  static FT_Bool
  af_coverage_iterator( AF_CoverageIterator  iter,
                        FT_UInt*             glyph )
  {
    if ( iter->p >= iter->limit )
      return FALSE;

    if ( iter->format == 1 )
      *glyph = FT_NEXT_USHORT( iter->p );
    else
    {
      if ( iter->glyph > iter->glyph_limit )
      {
        iter->glyph       = FT_NEXT_USHORT( iter->p );
        iter->glyph_limit = FT_NEXT_USHORT( iter->p );

        iter->p += 2;
      }

      *glyph = iter->glyph++;
    }

    return TRUE;
  }


  static AF_CoverageIteratorRec
  af_coverage_iterator_init( FT_Byte*  coverage )
  {
    AF_CoverageIteratorRec iterator;


    iterator.format      = af_coverage_format( coverage );
    iterator.p           = af_coverage_start( coverage );
    iterator.limit       = af_coverage_limit( coverage );
    iterator.glyph       = 1;
    iterator.glyph_limit = 0;

    return iterator;
  }


  /*
    Because we merge all single and alternate substitution mappings into
    one, large hash, we need the possibility to have multiple glyphs as
    values.  We utilize that we have 32bit integers but only 16bit glyph
    indices, using the following scheme.

    If glyph G maps to a single substitute S, the entry in the map is

      G  ->  S

    If glyph G maps to multiple substitutes S1, S2, ..., Sn, we do

      G                    ->  S1 + ((n - 1) << 16)
      G + (1 << 16)        ->  S2
      G + (2 << 16)        ->  S3
      ...
      G + ((n - 1) << 16)  ->  Sn
  */
  static FT_Error
  af_hash_insert( FT_UInt    glyph,
                  FT_UInt16  substitute,
                  FT_Hash    map,
                  FT_Memory  memory )
  {
    FT_Error  error;

    size_t*  value = ft_hash_num_lookup( glyph, map );


    if ( !value )
    {
      error = ft_hash_num_insert( glyph, substitute, map, memory );
      if ( error )
        return error;
    }
    else
    {
      /* Get number of substitutes, increased by one... */
      FT_UInt  mask = ( *value & 0xFFFF0000 ) + 0x10000U;


      /* ... which becomes the new key mask. */
      error = ft_hash_num_insert( glyph | mask, substitute, map, memory );
      if ( error )
        return error;

      /* Update number of substitutes. */
      *value += 0x10000U;
    }

    return FT_Err_Ok;
  }


  static FT_Error
  af_map_single_subst1( FT_Hash    map,
                        FT_Byte*   table,
                        FT_Memory  memory )
  {
    FT_Error  error;

    FT_UInt  coverageOffset;
    FT_UInt  deltaGlyphID;

    AF_CoverageIteratorRec  iterator;

    FT_UInt  glyph;


    coverageOffset = FT_PEEK_USHORT( table + 2 );
    deltaGlyphID   = FT_PEEK_USHORT( table + 4 );

    iterator = af_coverage_iterator_init( table + coverageOffset );

    while ( af_coverage_iterator( &iterator, &glyph ) )
    {
      /* `deltaGlyphID` requires modulo 65536 arithmetic. */
      FT_UInt  subst = (FT_UInt16)( ( glyph + deltaGlyphID ) % 0x10000 );


      error = af_hash_insert( glyph, subst, map, memory );
      if ( error )
        return error;
    }

    return FT_Err_Ok;
  }


  static FT_Error
  af_map_single_subst2( FT_Hash    map,
                        FT_Byte*   table,
                        FT_Memory  memory )
  {
    FT_Error  error;

    FT_UInt  coverageOffset;

    AF_CoverageIteratorRec  iterator;

    FT_UInt   glyph;
    FT_Byte*  p = table + 2;


    coverageOffset = FT_NEXT_USHORT( p );

    p += 2;

    iterator = af_coverage_iterator_init( table + coverageOffset );

    while ( af_coverage_iterator( &iterator, &glyph ) )
    {
      FT_UInt  subst = FT_NEXT_USHORT( p );


      error = af_hash_insert( glyph, subst, map, memory );
      if ( error )
        return error;
    }

    return FT_Err_Ok;
  }


  static FT_Error
  af_map_alternate( FT_Hash    map,
                    FT_Byte*   table,
                    FT_Memory  memory )
  {
    FT_Error  error;

    FT_UInt  coverageOffset;

    AF_CoverageIteratorRec  iterator;

    FT_UInt   glyph;
    FT_Byte*  p = table + 2;


    coverageOffset = FT_NEXT_USHORT( p );

    p += 2;

    iterator = af_coverage_iterator_init( table + coverageOffset );

    while ( af_coverage_iterator( &iterator, &glyph ) )
    {
      FT_UInt  alternateSetOffset = FT_NEXT_USHORT( p );

      FT_Byte*  q          = table + alternateSetOffset;
      FT_UInt   glyphCount = FT_NEXT_USHORT( q );

      FT_UInt  i;


      if ( glyphCount == 0 )
        continue;

      for ( i = 0; i < glyphCount; i++ )
      {
        FT_UInt  subst = FT_NEXT_USHORT( q );


        error = af_hash_insert( glyph, subst, map, memory );
        if ( error )
          return error;
      }
    }

    return FT_Err_Ok;
  }


  FT_LOCAL_DEF( FT_Error )
  af_map_lookup( AF_FaceGlobals  globals,
                 FT_Hash         map,
                 FT_UInt32       lookup_offset )
  {
    FT_Face    face   = globals->face;
    FT_Memory  memory = face->memory;

    FT_Byte*   table = globals->gsub + lookup_offset;

    FT_UInt  lookupType;
    FT_UInt  subtableCount;

    FT_Byte*  p;
    FT_Byte*  limit;


    lookupType = table[1];

    p = table + 4;

    subtableCount = FT_NEXT_USHORT( p );

    limit = p + subtableCount * 2;
    while ( p < limit )
    {
      FT_Error  error;

      FT_UInt   subtableOffset;
      FT_UInt   format;
      FT_Byte*  subtable;


      subtableOffset = FT_NEXT_USHORT( p );

      if ( lookupType == 7 )
      {
        FT_UInt  offset;


        lookupType = table[subtableOffset + 3];

        offset          = FT_PEEK_LONG( table + subtableOffset + 4 );
        subtableOffset += offset;
      }

      format   = table[subtableOffset + 1];
      subtable = table + subtableOffset;

      if ( lookupType == 1 )
        error = ( format == 1 )
                  ? af_map_single_subst1( map, subtable, memory )
                  : af_map_single_subst2( map, subtable, memory );
      else
        error = af_map_alternate( map, subtable, memory );

      if ( error )
        return error;
    }

    return FT_Err_Ok;
  }


#else /* !FT_CONFIG_OPTION_USE_HARFBUZZ */

/* ANSI C doesn't like empty source files */
typedef int  afgsub_dummy_;

#endif /* !FT_CONFIG_OPTION_USE_HARFBUZZ */

/* END */
