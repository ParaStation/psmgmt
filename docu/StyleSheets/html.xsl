<?xml version='1.0'?>
<!-- vim:set sts=2 shiftwidth=2 syntax=sgml: -->
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>

  <xsl:import href="http://docbook.sourceforge.net/release/xsl/current/html/chunk.xsl"/>

  <xsl:param name="make.year.ranges" select="1"></xsl:param>

  <xsl:param name="shade.verbatim" select="1"/>
  <xsl:param name="xref.with.number.and.title" select="0"/>
  <xsl:param name="preferred.mediaobject.role">HTML</xsl:param>

  <xsl:param name="generate.toc">
    book      toc,title
  </xsl:param>
   
  <xsl:template name="book.titlepage.recto">
    <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="title"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="subtitle"/>

    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/releaseinfo"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/pubdate"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/copyright"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/legalnotice"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/revision"/>
    <xsl:apply-templates
      mode="book.titlepage.recto.auto.mode" select="bookinfo/abstract"/>
  </xsl:template>

  <xsl:template match="abstract" mode="titlepage.mode">
    <div class="{name(.)}">
      <xsl:call-template name="anchor"/>
      <xsl:apply-templates mode="titlepage.mode"/>
    </div>
  </xsl:template>

  <xsl:template match="book" mode="object.title.markup">
    <xsl:param name="allow-anchors" select="0"/>
    <xsl:variable name="template">
      <xsl:apply-templates select="." mode="object.title.template"/>
    </xsl:variable>

    <xsl:call-template name="substitute-markup">
      <xsl:with-param name="allow-anchors" select="$allow-anchors"/>
      <xsl:with-param name="template" select="titleabbrev"/>
    </xsl:call-template>
  </xsl:template>

  <!-- ************** Modifications to html/synop.xsl *********** -->
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

  <!-- This fixes a bug in 1.60.1 also fixed in 1.61.2 -->
  <xsl:template match="glossentry/glosssee">
    <xsl:variable name="otherterm" select="@otherterm"/>
    <xsl:variable name="targets" select="//node()[@id=$otherterm]"/>
    <xsl:variable name="target" select="$targets[1]"/>

    <dd>
      <p>
	<xsl:call-template name="gentext.template">
	  <xsl:with-param name="context" select="'glossary'"/>
	  <xsl:with-param name="name" select="'see'"/>
	</xsl:call-template>
	<xsl:choose>
	  <xsl:when test="$target">
	    <a href="#{@otherterm}">
	      <xsl:apply-templates select="$target" mode="xref-to"/>
	    </a>
	  </xsl:when>
	  <xsl:when test="$otherterm != '' and not($target)">
	    <xsl:message>
	      <xsl:text>Warning: glosssee @otherterm reference not found: </xsl:text>
	      <xsl:value-of select="$otherterm"/>
	    </xsl:message>
	    <xsl:apply-templates/>
	  </xsl:when>
	  <xsl:otherwise>
	    <xsl:apply-templates/>
	  </xsl:otherwise>
	</xsl:choose>
	<xsl:text>.</xsl:text>
      </p>
    </dd>
  </xsl:template>

</xsl:stylesheet>
