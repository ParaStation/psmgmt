<!DOCTYPE style-sheet PUBLIC "-//James Clark//DTD DSSSL Style Sheet//EN" [
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

(define ($object-titles-after$)
  ;; List of objects who's titles go after the object
  (list (normalize "figure")))

(define %footer-margin% 
  ;; Height of footer margin
  6pi)

(define %title-font-family% "Helvetica")
(define %body-font-family% "Helvetica")

(define %graphic-extensions% 
  ;; The list of extensions which may appear on a 'fileref'
  ;; on a 'Graphic' which are indicative of graphic formats.
  '("pdf" "jpg" "jpeg" "png"))

(define preferred-mediaobject-notations
  (list "PDF"))

(define preferred-mediaobject-extensions
  (list "pdf"))

(define acceptable-mediaobject-notations
  (list "PNG"))

(define acceptable-mediaobject-extensions
  (list "png"))

;; Alternative fonts
;;(define %title-font-family% "iso-sanserif")
;;(define %body-font-family% "iso-sanserif")

;; Alternative fonts
;;(define %title-font-family% "Computer-Modern-Sans")
;;(define %body-font-family% "Computer-Modern-Sans")

(define (book-titlepage-recto-elements)
  (list (normalize "title")
	(normalize "releaseinfo")
	(normalize "pubdate")
	(normalize "mediaobject")))
					 
(define (book-titlepage-verso-elements)
  (list (normalize "titleabbrev")
	(normalize "releaseinfo")
	(normalize "author")
	(normalize "copyright")
	(normalize "legalnotice")
	(normalize "address")
	(normalize "abstract")
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
  (element title
    (make paragraph
      use: book-titlepage-recto-style
      font-size: (HSIZE 7)
      line-spacing: (* (HSIZE 7) %line-spacing-factor%)
      space-before: (* (HSIZE 9) %head-before-factor%)
      quadding: %division-title-quadding%
      keep-with-next?: #t
      heading-level: (if %generate-heading-level% 1 0)
      (with-mode title-mode
        (process-children-trim))))

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

  (element pubdate
    (make paragraph
      use: book-titlepage-recto-style
      font-size: (HSIZE 3)
      line-spacing: (* (HSIZE 7) %line-spacing-factor%)
      space-before: (* (HSIZE 9) %head-before-factor%)
      quadding: %division-title-quadding%
      keep-with-next?: #t
      (with-mode title-mode
        (process-children-trim))))

  (element mediaobject
    (make paragraph
      space-before: (* (HSIZE 9) 3.2)
      ($mediaobject$))))

(define (book-titlepage-before node side)
  (if (equal? side 'recto)
      (cond
       ((equal? (gi node) (normalize "title"))
        (make paragraph
          space-after: (* (HSIZE 5) %head-after-factor% 0)
          (literal "\no-break-space;")))
       (else (empty-sosofo)))
      (empty-sosofo)))

;;
;; New style for the revision history on second page:
;; - One line per revision
;; - No author info
;; - More space between lines
;;
(mode book-titlepage-verso-mode
  (element titleabbrev
    (make sequence
      font-family-name: %title-font-family%
      font-weight: 'bold
      font-size: (HSIZE 2)
      line-spacing: (* (HSIZE 2) %line-spacing-factor%)
      (process-children)))

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

;; The titleabbrev is handled quite simply
(element titleabbrev (make paragraph (process-children)))
;; Ignore the book/titleabbrev entries
(element (book titleabbrev) (empty-sosofo))

;; Ignore referenceinfo entries
(element referenceinfo (empty-sosofo))

;; Ignore refentryinfo entries
(element refentryinfo (empty-sosofo))

(define (titlepage-info-elements node info #!optional (intro (empty-node-list)))
  ;; Returns a node-list of the elements that might appear on a title
  ;; page.  This node-list is constructed as follows:
  ;;
  ;; 1. The "title" child of node is considered as a possibility
  ;; 2. The "titleabbrev" child of node is considered as a possibility
  ;; 3. If info is not empty, then node-list starts as the children
  ;;    of info.  If the children of info don't include a title, then
  ;;    the title from the node is added.
  ;; 4. If info is empty, then node-list starts as the children of node,
  ;;    but with "partintro" filtered out.

  (let* ((title (select-elements (children node) (normalize "title")))
	 (titleabb (select-elements (children node) (normalize "titleabbrev")))
         (nl    (if (node-list-empty? info)
                    (node-list-filter-by-not-gi (children node) 
                                                (list (normalize "partintro")))
                    (children info)))
         (nltitle (node-list-filter-by-gi nl (list (normalize "title")))))
    (if (node-list-empty? info)
        (node-list nl
                   intro)
        (node-list (if (node-list-empty? nltitle)
                       title
                       (empty-node-list))
		   titleabb
		   nl
                   intro))))


;; Some setup to get document title for footer entry
(define ($doctitle-header-footer-element$)
  (let* ((firstancestor (node-list-first (ancestors (current-node))))
         (metainfo   (if (node-list-empty? firstancestor)
                         (current-node)
			 firstancestor))
         (metatitle  (select-elements (children metainfo)
				      (normalize "title")))
         (metatabb   (select-elements (children metainfo)
				      (normalize "titleabbrev")))

         (title      (select-elements (children (current-node)) 
                                      (normalize "title")))
         (titleabb   (select-elements (children (current-node)) 
                                      (normalize "titleabbrev"))))
    (if (node-list-empty? metatabb)
        (if (node-list-empty? titleabb)
            (if (node-list-empty? metatitle)
                title
                metatitle)
            titleabb)
        metatabb)))
(define ($doctitle-header-footer$)
  (let* ((title ($doctitle-header-footer-element$)))
    (make sequence
      font-family-name: %title-font-family%
      (with-mode hf-mode 
        (process-node-list title)))))

;; Inner footer always document title
(define (first-page-inner-footer gi)
  (cond
   ((equal? (normalize gi) (normalize "dedication")) (empty-sosofo))
   ((equal? (normalize gi) (normalize "part")) (empty-sosofo))
   (else ($doctitle-header-footer$))))
(define (page-inner-footer gi)
  (cond
   ((equal? (normalize gi) (normalize "dedication")) (empty-sosofo))
   ((equal? (normalize gi) (normalize "part")) (empty-sosofo))
   (else ($doctitle-header-footer$))))

(define ($page-number-header-footer$) 
  (let ((component (ancestor-member (current-node) 
                                    (append (division-element-list)
                                            (component-element-list)))))
    (make sequence
      font-family-name: %title-font-family%
      ;; font-posture: 'italic
      (literal 
       (gentext-page)
       (if %page-number-restart%
           (cond
            ((equal? (gi component) (normalize "appendix") ) 
             (string-append
              (element-label component #t)
              (gentext-intra-label-sep "_pagenumber")))
            ((equal? (gi component) (normalize "chapter"))
             (string-append
              (element-label component #t)
              (gentext-intra-label-sep "_pagenumber")))
            (else ""))
           ""))
      (page-number-sosofo))))

</style-specification-body>
</style-specification>

<external-specification id="docbook" document="docbook.dsl">

</style-sheet>
