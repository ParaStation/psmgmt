<?xml version='1.0'?>
<!-- vim:set sts=2 shiftwidth=2 syntax=sgml: -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

  <xsl:import href="/usr/share/sgml/docbook/docbook-xsl-stylesheets/fo/docbook.xsl"/>

  <xsl:param name="shade.verbatim" select="1"/>
  <xsl:param name="paper.type" select="'A4'"/>

  <!-- Shall we use extensions -->
  <!-- PDF bookmarks and index terms -->
  <!-- xep is PDF bookmarks and document information -->
  <xsl:param name="use.extensions" select="'1'"/>   <!-- must be on -->
  <xsl:param name="xep.extensions" select="0"/>      
  <xsl:param name="fop.extensions" select="0"/>     
  <xsl:param name="saxon.extensions" select="0"/>   
  <xsl:param name="passivetex.extensions" select="1"/>
  <xsl:param name="tablecolumns.extension" select="'1'"/>

  <!-- ************** Modifications to manpages/synop.xsl *********** -->
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

</xsl:stylesheet>
