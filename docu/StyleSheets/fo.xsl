<?xml version='1.0'?>
<!-- vim:set sts=2 shiftwidth=2 syntax=sgml: -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:fo="http://www.w3.org/1999/XSL/Format"
                version='1.0'>

  <xsl:import href="http://docbook.sourceforge.net/release/xsl/current/fo/docbook.xsl"/>

  <xsl:param name="shade.verbatim" select="1"/>
  <xsl:param name="paper.type" select="'A4'"/>

  <!-- Shall we use extensions -->
  <!-- PDF bookmarks and index terms -->
  <!-- xep is PDF bookmarks and document information -->
  <xsl:param name="use.extensions" select="'1'"/>   <!-- must be on -->
  <xsl:param name="xep.extensions" select="0"/>      
  <xsl:param name="fop.extensions" select="1"/>     
  <xsl:param name="saxon.extensions" select="0"/>   
  <xsl:param name="passivetex.extensions" select="0"/>
  <xsl:param name="tablecolumns.extension" select="'1'"/>

  <xsl:param name="body.font.master">12</xsl:param>
  <xsl:param name="double.sided" select="1"></xsl:param>
  <xsl:param name="body.font.family" select="'sans-serif'"></xsl:param>
  <xsl:param name="draft.mode" select="'no'"></xsl:param>
  <xsl:param name="generate.toc">book  toc,title</xsl:param>
  <xsl:param name="make.year.ranges" select="1"></xsl:param>

  <xsl:param name="formal.title.placement">figure after</xsl:param>

  <xsl:attribute-set name="footnote.sep.leader.properties">
    <xsl:attribute name="color">black</xsl:attribute>
    <xsl:attribute name="leader-pattern">rule</xsl:attribute>
    <xsl:attribute name="leader-length">0.5in</xsl:attribute>
  </xsl:attribute-set>

  <xsl:param name="header.column.widths" select="'4 1 1'" />
  <xsl:param name="footer.column.widths" select="'4 1 1'" />

  <xsl:param name="section.autolabel" select="1" />
  <xsl:param name="section.label.includes.component.label" select="1" />
  <xsl:param name="ulink.show" select="0" />

  <xsl:param name="variablelist.as.blocks" select="1" />
<xsl:attribute-set name="list.item.spacing">
  <xsl:attribute name="space-before.optimum">1em</xsl:attribute>
  <xsl:attribute name="space-before.minimum">0.8em</xsl:attribute>
  <xsl:attribute name="space-before.maximum">1.2em</xsl:attribute>
  <xsl:attribute name="space-after.minimum">0.4em</xsl:attribute>
  <xsl:attribute name="space-after.optimum">0.5em</xsl:attribute>
  <xsl:attribute name="space-after.maximum">0.8em</xsl:attribute>
</xsl:attribute-set>

  <xsl:param name="page.margin.inner" select="'1in'" />
  <xsl:param name="page.margin.outer" select="'0.8in'" />
  <xsl:param name="title.margin.left" select="'0pt'" />


<xsl:attribute-set name="section.title.level1.properties">
  <xsl:attribute name="font-size">
    <xsl:value-of select="$body.font.master * 1.728"></xsl:value-of>
    <xsl:text>pt</xsl:text>
  </xsl:attribute>
</xsl:attribute-set>
<xsl:attribute-set name="section.title.level2.properties">
  <xsl:attribute name="font-size">
    <xsl:value-of select="$body.font.master * 1.44"></xsl:value-of>
    <xsl:text>pt</xsl:text>
  </xsl:attribute>
</xsl:attribute-set>
<xsl:attribute-set name="section.title.level3.properties">
  <xsl:attribute name="font-size">
    <xsl:value-of select="$body.font.master * 1.2"></xsl:value-of>
    <xsl:text>pt</xsl:text>
  </xsl:attribute>
</xsl:attribute-set>
<xsl:attribute-set name="section.title.properties">
  <xsl:attribute name="font-family">
    <xsl:value-of select="$title.font.family"/>
  </xsl:attribute>
  <xsl:attribute name="font-weight">bold</xsl:attribute>
  <!-- font size is calculated dynamically by section.heading template -->
  <xsl:attribute name="keep-with-next.within-column">always</xsl:attribute>
  <xsl:attribute name="space-before.minimum">2.8em</xsl:attribute>
  <xsl:attribute name="space-before.optimum">3.0em</xsl:attribute>
  <xsl:attribute name="space-before.maximum">3.2em</xsl:attribute>
</xsl:attribute-set>


  <xsl:param name="preferred.mediaobject.role">FO-PDF</xsl:param>

  <xsl:include href="fo-titlepage.xsl"/> 



  <!-- ************** Modifications to synop.xsl *********** -->
  <!-- Display commands in bold style, no newline after command -->
  <xsl:template match="cmdsynopsis/command">
    <xsl:call-template name="inline.boldseq"/>
    <xsl:text> </xsl:text>
  </xsl:template>
  <xsl:template match="cmdsynopsis/command[1]" priority="2">
    <xsl:call-template name="inline.boldseq"/>
    <xsl:text> </xsl:text>
  </xsl:template>

  <!-- Put repeat string after group -->
  <xsl:template match="group|arg">
    <xsl:variable name="choice" select="@choice"/>
    <xsl:variable name="rep" select="@rep"/>
    <xsl:variable name="sepchar">
      <xsl:choose>
	<xsl:when test="ancestor-or-self::*/@sepchar">
	  <xsl:value-of select="ancestor-or-self::*/@sepchar"/>
	</xsl:when>
	<xsl:otherwise>
	  <xsl:text> </xsl:text>
	</xsl:otherwise>
      </xsl:choose>
    </xsl:variable>
    <xsl:if test="position()>1"><xsl:value-of select="$sepchar"/></xsl:if>
    <xsl:choose>
      <xsl:when test="$choice='plain'">
	<xsl:value-of select="$arg.choice.plain.open.str"/>
      </xsl:when>
      <xsl:when test="$choice='req'">
	<xsl:value-of select="$arg.choice.req.open.str"/>
      </xsl:when>
      <xsl:when test="$choice='opt'">
	<xsl:value-of select="$arg.choice.opt.open.str"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="$arg.choice.def.open.str"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:apply-templates/>
    <xsl:choose>
      <xsl:when test="$choice='plain'">
	<xsl:value-of select="$arg.choice.plain.close.str"/>
      </xsl:when>
      <xsl:when test="$choice='req'">
	<xsl:value-of select="$arg.choice.req.close.str"/>
      </xsl:when>
      <xsl:when test="$choice='opt'">
	<xsl:value-of select="$arg.choice.opt.close.str"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="$arg.choice.def.close.str"/>
      </xsl:otherwise>
    </xsl:choose>
    <xsl:choose>
      <xsl:when test="$rep='repeat'">
	<xsl:value-of select="$arg.rep.repeat.str"/>
      </xsl:when>
      <xsl:when test="$rep='norepeat'">
	<xsl:value-of select="$arg.rep.norepeat.str"/>
      </xsl:when>
      <xsl:otherwise>
	<xsl:value-of select="$arg.rep.def.str"/>
      </xsl:otherwise>
    </xsl:choose>
  </xsl:template>

  <xsl:template match="group/arg">
    <xsl:variable name="choice" select="@choice"/>
    <xsl:variable name="rep" select="@rep"/>
    <xsl:if test="position()>1"><xsl:value-of select="$arg.or.sep"/></xsl:if>
    <xsl:apply-templates/>
  </xsl:template>

  <xsl:template match="bookinfo/pubdate" mode="titlepage.mode" priority="2">
    <xsl:apply-templates mode="titlepage.mode"/>
  </xsl:template>

  <!-- *************** Declare header and footer ************ -->
<xsl:template name="header.content">
  <xsl:param name="pageclass" select="''"/>
  <xsl:param name="sequence" select="''"/>
  <xsl:param name="position" select="''"/>
  <xsl:param name="gentext-key" select="''"/>

  <fo:block>

    <!-- sequence can be odd, even, first, blank -->
    <!-- position can be left, center, right -->
    <xsl:choose>
      <xsl:when test="$sequence = 'blank'">
        <!-- nothing -->
      </xsl:when>

      <xsl:when test="$position='center'">
        <!-- nothing -->
      </xsl:when>

      <xsl:when test="$sequence = 'first'">
        <!-- nothing for first pages -->
      </xsl:when>

      <xsl:when test="$sequence = 'blank'">
        <!-- nothing for blank pages -->
      </xsl:when>

      <xsl:when test="$double.sided != 0 and (
                        ($sequence = 'even' and $position='right') or
                        ($sequence = 'odd' and $position='left'))">
         <xsl:choose>
           <xsl:when test="ancestor::book and ($double.sided != 0)">
             <fo:retrieve-marker retrieve-class-name="section.head.marker"
                                 retrieve-position="first-including-carryover"
                                 retrieve-boundary="page-sequence"/>
           </xsl:when>
           <xsl:otherwise>
             <xsl:apply-templates select="." mode="titleabbrev.markup"/>
           </xsl:otherwise>
         </xsl:choose>
      </xsl:when>

      <xsl:when test="$double.sided != 0 and (
                        ($sequence = 'even' and $position='left') or
                        ($sequence = 'odd' and $position='right'))">
         <xsl:choose>
           <xsl:when test="ancestor::book and ($double.sided != 0)">
        <!-- <xsl:apply-templates select="." mode="object.title.markup"/> -->
           </xsl:when>
           <xsl:otherwise>
        <!-- <xsl:apply-templates select="." mode="titleabbrev.markup"/> -->
           </xsl:otherwise>
         </xsl:choose>
      </xsl:when>

      <xsl:otherwise>
        <!-- nop -->
      </xsl:otherwise>

    </xsl:choose>
  </fo:block>
</xsl:template>

<xsl:template name="footer.content">
  <xsl:param name="pageclass" select="''"/>
  <xsl:param name="sequence" select="''"/>
  <xsl:param name="position" select="''"/>
  <xsl:param name="gentext-key" select="''"/>

  <fo:block>
    <!-- pageclass can be front, body, back -->
    <!-- sequence can be odd, even, first, blank -->
    <!-- position can be left, center, right -->
    <xsl:choose>
      <xsl:when test="$pageclass = 'titlepage'">
        <!-- nop; no footer on title pages -->
      </xsl:when>

      <xsl:when test="$double.sided != 0 and
             ($sequence = 'even' or $sequence = 'blank') and $position='left'">
        <fo:page-number/>
      </xsl:when>

      <xsl:when test="$double.sided != 0 and
             ($sequence = 'odd' or $sequence = 'first') and $position='right'">
        <fo:page-number/>
      </xsl:when>

      <xsl:when test="$double.sided = 0 and $position='center'">
        <fo:page-number/>
      </xsl:when>

      <xsl:when test="$double.sided != 0 and 
        ((($sequence = 'odd' or $sequence = 'first') and $position='left') or
         (($sequence = 'even' or $sequence = 'blank') and $position='right'))">
        <xsl:value-of select="ancestor-or-self::book/titleabbrev"/>
      </xsl:when>

      <xsl:otherwise>
        <!-- nop -->
      </xsl:otherwise>
    </xsl:choose>
  </fo:block>
</xsl:template>

  <!-- *************** Nicer revision history ************ -->

<xsl:template match="revhistory" mode="book.titlepage.verso.mode">
  <fo:table table-layout="fixed" space-before="3em" width="100%">
    <fo:table-column column-number="1" column-width="proportional-column-width(1)"/>
    <fo:table-column column-number="2" column-width="proportional-column-width(1)"/>
    <fo:table-column column-number="3" column-width="proportional-column-width(0.1)"/>
    <fo:table-column column-number="4" column-width="proportional-column-width(4)"/>
    <fo:table-body>
      <fo:table-row>
        <fo:table-cell number-columns-spanned="3">
          <fo:block font-size="{$body.font.size}*1.2" space-after="0.5em">
            <xsl:call-template name="gentext">
              <xsl:with-param name="key" select="'RevHistory'"/>
            </xsl:call-template>
          </fo:block>
        </fo:table-cell>
      </fo:table-row>
      <xsl:apply-templates mode="book.titlepage.verso.mode"/>
    </fo:table-body>
  </fo:table>
</xsl:template>

<xsl:template match="revhistory/revision" mode="book.titlepage.verso.mode">
  <xsl:variable name="revnumber" select=".//revnumber"/>
  <xsl:variable name="revdate"   select=".//date"/>
  <xsl:variable name="revremark" select=".//revremark|.//revdescription"/>
  <fo:table-row>
    <fo:table-cell>
      <fo:block>
        <xsl:if test="$revnumber">
          <xsl:call-template name="gentext">
            <xsl:with-param name="key" select="'Revision'"/>
          </xsl:call-template>
          <xsl:call-template name="gentext.space"/>
          <xsl:apply-templates select="$revnumber[1]"/>
        </xsl:if>
      </fo:block>
    </fo:table-cell>
    <fo:table-cell text-align="right">
      <fo:block>
        <xsl:apply-templates select="$revdate[1]"/>
      </fo:block>
    </fo:table-cell>
    <fo:table-cell/>
    <fo:table-cell>
      <fo:block>
        <xsl:apply-templates select="$revremark[1]"/>
      </fo:block>
    </fo:table-cell>
  </fo:table-row>
</xsl:template>


</xsl:stylesheet>
