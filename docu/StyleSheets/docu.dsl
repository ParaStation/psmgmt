<!DOCTYPE style-sheet PUBLIC "-//James Clark//DTD DSSSL Style Sheet//EN" [
<!ENTITY % html "IGNORE">
<![%html;[
<!ENTITY % print "IGNORE">
<!ENTITY docbook.dsl SYSTEM "/usr/share/sgml/docbook/dsssl-stylesheets/html/docbook.dsl" CDATA dsssl>
]]>
<!ENTITY % print "INCLUDE">
<![%print;[
<!ENTITY docbook.dsl SYSTEM "/usr/share/sgml/docbook/dsssl-stylesheets/print/docbook.dsl" CDATA dsssl>
]]>
]>

<!-- Copyright (C) 1999 by SuSE GmbH -->
<!-- Karl Eichwalder <ke@suse.de> -->
<!-- GPL  -->
<!-- based on Cygnus customizations by Mark Galassi -->

<style-sheet>

<style-specification id="print" use="docbook">
<style-specification-body> 

;; ====================
;; customize the print stylesheet
;; ====================

;; make funcsynopsis look pretty
(define %funcsynopsis-decoration%
  ;; Decorate elements of a FuncSynopsis?
  #t)

(define %refentry-new-page% 
  ;; 'RefEntry' starts on new page?
  #t)

(define %body-start-indent% 
  ;; Default indent of body text
  0pt)

;; use graphics in admonitions, and have their path be "."
;; NO: we are not yet ready to use gifs in TeX and so forth
(define %admon-graphics-path%
  "./")
(define %admon-graphics%
  #f)
(define %hyphenation% #t)
(define %default-quadding% 'justify)

;; this is necessary because right now jadetex does not understand
;; symbolic entities, whereas things work well with numeric entities.
(declare-characteristic preserve-sdata?
          "UNREGISTERED::James Clark//Characteristic::preserve-sdata?"
          #f)
(define %two-side% #t)
(define %paper-type% "A4")

(define %section-autolabel% 
  ;; Are sections enumerated?
  #t)

;; (define %title-font-family% 
;;   ;; The font family used in titles
;;   "Ariel")
(define %visual-acuity%
  ;; General measure of document text size
  ;; "presbyopic"
  ;; "large-type"
  "presbyopic")

(define bop-footnotes
  ;; Make "bottom-of-page" footnotes?
  #t)

(define ($generate-book-lot-list$)
  ;; Which Lists of Titles should be produced for Books?
  (list ))

;; (define %block-start-indent% 10pt)

(define %footer-margin% 
  ;; Height of footer margin
  6pi)

;; (define %graphic-default-extension% "eps")

(define (book-titlepage-recto-elements)
  (list (normalize "mediaobject")
	(normalize "title")
	(normalize "releaseinfo")
	(normalize "author")))
					 
(define (book-titlepage-verso-elements)
  (list (normalize "title")
	(normalize "releaseinfo")
	(normalize "author")
	(normalize "copyright")
	(normalize "revhistory")))
					 
;;
;; The next two ones are for removing extra newlines on multiple 'command's in
;; a 'cmdsynopsis'.
;;
(element command ($bold-seq$))
(element (cmdsynopsis command)
  (make sequence
    (if (first-sibling? (current-node))
        (empty-sosofo)
        (literal ""))
    (next-match)
    (literal " ")))

;;
;; Print '}...' instead of '...}' closing a 'group rep="repeat"'.
;;
(element group
  (let ((choice  (attribute-string (normalize "choice")))
        (rep     (attribute-string (normalize "rep")))
        (sepchar (if (inherited-attribute-string (normalize "sepchar"))
                     (inherited-attribute-string (normalize "sepchar"))
                     " ")))
    (make sequence
      (if (equal? (absolute-child-number (current-node)) 1)
          (empty-sosofo)
          (literal sepchar))
      (cond
       ((equal? choice (normalize "plain")) (literal %arg-choice-plain-open-str%))
       ((equal? choice (normalize "req")) (literal %arg-choice-req-open-str%))
       ((equal? choice (normalize "opt")) (literal %arg-choice-opt-open-str%))
       (else (literal %arg-choice-def-open-str%)))
      (process-children)
      (cond
       ((equal? choice (normalize "plain")) (literal %arg-choice-plain-close-str%))
       ((equal? choice (normalize "req")) (literal %arg-choice-req-close-str%))
       ((equal? choice (normalize "opt")) (literal %arg-choice-opt-close-str%))
       (else (literal %arg-choice-def-close-str%)))
      (cond
       ((equal? rep (normalize "repeat")) (literal %arg-rep-repeat-str%))
       ((equal? rep (normalize "norepeat")) (literal %arg-rep-norepeat-str%))
       (else (literal %arg-rep-def-str%))))))

;;
;; Print ']...' instead of '...]' closing a 'arg rep="repeat"'.
;;
(element arg
  (let ((choice  (attribute-string (normalize "choice")))
        (rep     (attribute-string (normalize "rep")))
        (sepchar (if (inherited-attribute-string (normalize "sepchar"))
                     (inherited-attribute-string (normalize "sepchar"))
                     " ")))
    (make sequence
      (if (equal? (absolute-child-number (current-node)) 1)
          (empty-sosofo)
          (literal sepchar))
      (cond
       ((equal? choice (normalize "plain")) (literal %arg-choice-plain-open-str%))
       ((equal? choice (normalize "req")) (literal %arg-choice-req-open-str%))
       ((equal? choice (normalize "opt")) (literal %arg-choice-opt-open-str%))
       (else (literal %arg-choice-def-open-str%)))
      (process-children)
      (cond
       ((equal? choice (normalize "plain")) (literal %arg-choice-plain-close-str%))
       ((equal? choice (normalize "req")) (literal %arg-choice-req-close-str%))
       ((equal? choice (normalize "opt")) (literal %arg-choice-opt-close-str%))
       (else (literal %arg-choice-def-close-str%)))
      (cond
       ((equal? rep (normalize "repeat")) (literal %arg-rep-repeat-str%))
       ((equal? rep (normalize "norepeat")) (literal %arg-rep-norepeat-str%))
       (else (literal %arg-rep-def-str%))))))

(element (group arg)
  (let ((choice (attribute-string (normalize "choice")))
        (rep (attribute-string (normalize "rep"))))
    (make sequence
      (if (not (first-sibling? (current-node)))
          (literal %arg-or-sep%)
          (empty-sosofo))
      (process-children))))


;;
;; Restart page numbering at *start* of book (i.e. before TOC)
;;
(element book 
  (let* ((bookinfo  (select-elements (children (current-node)) 
                                     (normalize "bookinfo")))
         (dedication (select-elements (children (current-node)) 
                                      (normalize "dedication")))
         (nl        (titlepage-info-elements (current-node) bookinfo)))
    (make sequence
      (if %generate-book-titlepage%
          (make simple-page-sequence
            page-n-columns: %titlepage-n-columns%
	    page-number-restart?: #t
            input-whitespace-treatment: 'collapse
            use: default-text-style
            (book-titlepage nl 'recto)
            (make display-group
              break-before: 'page
              (book-titlepage nl 'verso)))
          (empty-sosofo))

      (if (node-list-empty? dedication)
          (empty-sosofo)
          (with-mode dedication-page-mode
            (process-node-list dedication)))

      (if %generate-book-toc%
          (make simple-page-sequence
            page-n-columns: %page-n-columns%
            page-number-format: ($page-number-format$ (normalize "toc"))
            use: default-text-style
            left-header:   ($left-header$ (normalize "toc"))
            center-header: ($center-header$ (normalize "toc"))
            right-header:  ($right-header$ (normalize "toc"))
            left-footer:   ($left-footer$ (normalize "toc"))
            center-footer: ($center-footer$ (normalize "toc"))
            right-footer:  ($right-footer$ (normalize "toc"))
            input-whitespace-treatment: 'collapse
            (build-toc (current-node)
                       (toc-depth (current-node))))
          (empty-sosofo))

      (process-children)
      (empty-sosofo))))

;;
;; Don't append '(url)' if type in ulink is given.
;;
(element ulink 
  (make sequence
    (if (node-list-empty? (children (current-node)))
        (literal (attribute-string (normalize "url")))
        (make sequence
          ($charseq$)
          (if (and (not (equal? (attribute-string (normalize "url"))
				(data-of (current-node))))
		   (not (attribute-string (normalize "type"))))
              (if %footnote-ulinks%
                  (if (and (equal? (print-backend) 'tex) bop-footnotes)
                      (make sequence
                        ($ss-seq$ + (literal (footnote-number (current-node))))
                        (make page-footnote
                          (make paragraph
                            font-family-name: %body-font-family%
                            font-size: (* %footnote-size-factor% %bf-size%)
                            font-posture: 'upright
                            quadding: %default-quadding%
                            line-spacing: (* (* %footnote-size-factor% %bf-size%
)
                                             %line-spacing-factor%)
                            space-before: %para-sep%
                            space-after: %para-sep%
                            start-indent: %footnote-field-width%
                            first-line-start-indent: (- %footnote-field-width%)
                            (make line-field
                              field-width: %footnote-field-width%
                              (literal (footnote-number (current-node))
                                       (gentext-label-title-sep (normalize "foot
note"))))
                            (literal (attribute-string (normalize "url"))))))
                      ($ss-seq$ + (literal (footnote-number (current-node)))))
                  (if %show-ulinks%
                      (make sequence
                        (literal " (")
                        (literal (attribute-string (normalize "url")))
                        (literal ")"))
                      (empty-sosofo)))
              (empty-sosofo))))))

;;
;; New styles for the title and the release info on the front page
;;
(mode book-titlepage-recto-mode
  (element releaseinfo
    (make paragraph
      use: book-titlepage-recto-style
      font-size: (HSIZE 4)
      line-spacing: (* (HSIZE 7) %line-spacing-factor%)
      space-before: (* (HSIZE 9) %head-before-factor%)
      quadding: %division-title-quadding%
      keep-with-next?: #t
      (with-mode title-mode
        (process-children-trim))))

  (element title 
    (make paragraph
      use: book-titlepage-recto-style
      font-size: (HSIZE 7)
      line-spacing: (* (HSIZE 7) %line-spacing-factor%)
      space-before: (* (HSIZE 7) %head-before-factor%)
      quadding: %division-title-quadding%
      keep-with-next?: #t
      heading-level: (if %generate-heading-level% 1 0)
      (with-mode title-mode
        (process-children-trim)))))

;;
;; New style for the revision history on second page:
;; - One line per revision
;; - No author info
;; - More space between lines
;;
(mode book-titlepage-verso-mode
  (element (revhistory revision)
    (let ((revnumber (select-elements (descendants (current-node)) 
                                      (normalize "revnumber")))
          (revdate   (select-elements (descendants (current-node)) 
                                      (normalize "date")))
          (revauthor (select-elements (descendants (current-node))
                                      (normalize "authorinitials")))
          (revremark (select-elements (descendants (current-node))
                                      (normalize "revremark"))))
      (make sequence
        (make table-row
          (make table-cell
            column-number: 1
            n-columns-spanned: 1
            n-rows-spanned: 1
            start-indent: 0pt
            (if (not (node-list-empty? revnumber))
                (make paragraph
                  use: book-titlepage-verso-style
                  font-size: %bf-size%
                  font-weight: 'medium
                  (literal (gentext-element-name-space (current-node)))
                  (process-node-list revnumber))
                (empty-sosofo)))
          (make table-cell
            column-number: 2
            n-columns-spanned: 1
            n-rows-spanned: 1
            start-indent: 0pt
            cell-before-column-margin: (if (equal? (print-backend) 'tex)
                                           6pt
                                           0pt)
            (if (not (node-list-empty? revdate))
                (make paragraph
                  use: book-titlepage-verso-style
                  font-size: %bf-size%
                  font-weight: 'medium
                  (process-node-list revdate))
                (empty-sosofo)))
          (make table-cell
            column-number: 3
            n-columns-spanned: 1
            n-rows-spanned: 1
            start-indent: 0pt
            cell-before-column-margin: (if (equal? (print-backend) 'tex)
                                           6pt
                                           0pt)
	    cell-after-row-margin: (/ (* (HSIZE 1) %head-before-factor%) 3)
            (if (not (node-list-empty? revremark))
                (make paragraph
                  use: book-titlepage-verso-style
                  font-size: %bf-size%
                  font-weight: 'medium
                  space-after: (if (last-sibling?) 
                                   0pt
                                   (/ %block-sep% 2))
                  (process-node-list revremark))
                (empty-sosofo))))))))

</style-specification-body>
</style-specification>

<external-specification id="docbook" document="docbook.dsl">

</style-sheet>
