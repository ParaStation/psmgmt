<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">

<!-- This stylesheet was created by template/titlepage.xsl; do not edit it by hand. -->

<xsl:template name="book.titlepage.recto">
  <xsl:choose>
    <xsl:when test="bookinfo/title">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="bookinfo/title"/>
    </xsl:when>
    <xsl:when test="info/title">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="info/title"/>
    </xsl:when>
    <xsl:when test="title">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="title"/>
    </xsl:when>
  </xsl:choose>

  <xsl:choose>
    <xsl:when test="bookinfo/subtitle">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="bookinfo/subtitle"/>
    </xsl:when>
    <xsl:when test="info/subtitle">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="info/subtitle"/>
    </xsl:when>
    <xsl:when test="subtitle">
      <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="subtitle"/>
    </xsl:when>
  </xsl:choose>

  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="bookinfo/releaseinfo"/>
  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="info/releaseinfo"/>
  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="bookinfo/pubdate"/>
  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="info/pubdate"/>
  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="bookinfo/mediaobject"/>
  <xsl:apply-templates mode="book.titlepage.recto.auto.mode" select="info/mediaobject"/>
</xsl:template>

<xsl:template name="book.titlepage.verso">
  <xsl:choose>
    <xsl:when test="bookinfo/titleabbrev">
      <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/titleabbrev"/>
    </xsl:when>
    <xsl:when test="info/titleabbrev">
      <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/titleabbrev"/>
    </xsl:when>
    <xsl:when test="titleabbrev">
      <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="titleabbrev"/>
    </xsl:when>
  </xsl:choose>

  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/releaseinfo"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/releaseinfo"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/author"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/author"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/copyright"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/copyright"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/legalnotice"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/legalnotice"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/address"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/address"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/abstract"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/abstract"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="bookinfo/revhistory"/>
  <xsl:apply-templates mode="book.titlepage.verso.auto.mode" select="info/revhistory"/>
</xsl:template>

<xsl:template name="book.titlepage.separator">
</xsl:template>

<xsl:template name="book.titlepage">
  <fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" font-family="{$title.font.family}">
    <xsl:variable name="recto.content">
      <xsl:call-template name="book.titlepage.before.recto"/>
      <xsl:call-template name="book.titlepage.recto"/>
    </xsl:variable>
    <xsl:if test="normalize-space($recto.content) != ''">
      <fo:block><xsl:copy-of select="$recto.content"/></fo:block>
    </xsl:if>
    <xsl:variable name="verso.content">
      <xsl:call-template name="book.titlepage.before.verso"/>
      <xsl:call-template name="book.titlepage.verso"/>
    </xsl:variable>
    <xsl:if test="normalize-space($verso.content) != ''">
      <fo:block><xsl:copy-of select="$verso.content"/></fo:block>
    </xsl:if>
    <xsl:call-template name="book.titlepage.separator"/>
  </fo:block>
</xsl:template>

<xsl:template match="*" mode="book.titlepage.recto.mode">
  <!-- if an element isn't found in this mode, -->
  <!-- try the generic titlepage.mode -->
  <xsl:apply-templates select="." mode="titlepage.mode"/>
</xsl:template>

<xsl:template match="*" mode="book.titlepage.verso.mode">
  <!-- if an element isn't found in this mode, -->
  <!-- try the generic titlepage.mode -->
  <xsl:apply-templates select="." mode="titlepage.mode"/>
</xsl:template>

<xsl:template match="title" mode="book.titlepage.recto.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.recto.style" font-size="{$body.font.size}*3.4" space-before="2cm">
<xsl:apply-templates select="." mode="book.titlepage.recto.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="subtitle" mode="book.titlepage.recto.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.recto.style" font-size="{$body.font.size}*3.4" space-before="0.5em">
<xsl:apply-templates select="." mode="book.titlepage.recto.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="releaseinfo" mode="book.titlepage.recto.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.recto.style" font-size="{$body.font.size}*2.076" space-before="2.5cm">
<xsl:apply-templates select="." mode="book.titlepage.recto.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="pubdate" mode="book.titlepage.recto.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.recto.style" font-size="{$body.font.size}*1.73" space-before="2.5cm">
<xsl:apply-templates select="." mode="book.titlepage.recto.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="mediaobject" mode="book.titlepage.recto.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.recto.style" space-before="5cm">
<xsl:apply-templates select="." mode="book.titlepage.recto.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="titleabbrev" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}*1.44" font-weight="bold">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="releaseinfo" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="author" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="copyright" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="legalnotice" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}*0.833">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="address" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}" text-align="start">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="abstract" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

<xsl:template match="revhistory" mode="book.titlepage.verso.auto.mode">
<fo:block xmlns:fo="http://www.w3.org/1999/XSL/Format" xsl:use-attribute-sets="book.titlepage.verso.style" font-size="{$body.font.size}">
<xsl:apply-templates select="." mode="book.titlepage.verso.mode"/>
</fo:block>
</xsl:template>

</xsl:stylesheet>
