<?php
/**
 * @file
 * Zanata REST API Communication.
 */

/**
 * Handles operations relating to Zanata URLs.
 */
class TMGMTZanataTranslatorRESTClient {

  private $serverUrl;
  private $proxy;
  private $projectId;
  private $versionId;
  private $username;
  private $apiKey;

  /**
   * Create a new connector for REST Communication.
   */
  public function __construct(TMGMTJob $job) {

    $translator = $job->getTranslator();

    $this->serverUrl = $translator->getSetting('server');
    $this->proxy = $translator->getSetting('proxy');
    $this->projectId = $translator->getSetting('project');
    $this->versionId = $translator->getSetting('version');
    $this->username = $translator->getSetting('username');
    $this->apiKey = $translator->getSetting('api_key');

    $this->job = $job;
    $this->translator = $translator;
    $this->controller = $translator->getController();
  }

  /**
   * Get the remote locale for the translation job.
   */
  public function getLocale() {
    return $this->controller->mapToRemoteLanguage($this->translator,
            $this->job->target_language);
  }

  /**
   * Generate the URL for the translation editor in Zanata.
   *
   * This will be the URL to the document list in the editor, ready for a node
   * to be selected.
   */
  public function getEditorUrl() {
    $locale = $this->getLocale();
    return "{$this->serverUrl}/webtrans/translate?project={$this->projectId}"
        . "&iteration={$this->versionId}&localeId={$locale}&locale=en";
  }

  /**
   * Generate the URL for the translation editor in Zanata for a Doc.
   *
   * This will be the URL to the document in the editor, ready for a node
   * to be translated.
   */
  public function getDocEditorUrl($job_item) {
    return $this->getEditorUrl() . '#view:doc;doc:' . $this->getDocId($job_item);
  }

  /**
   * Generate a base URL for documents in a project/version.
   *
   * @return string
   *   the base REST URL for documents in the configured project/version
   */
  private function getBaseUrl() {
    return "{$this->serverUrl}/rest/projects/p/{$this->projectId}/"
        . "iterations/i/{$this->versionId}/r";
  }

  /**
   * Generate a document ID for a given job item.
   *
   * @return string
   *   the unencoded document id on Zanata for the given job item
   */
  public function getDocId($job_item) {
    $id = $job_item->item_id;
    $type = $job_item->item_type;

    // Returns an associative array with id as key.
    $loaded_entity = entity_load($type, array($id));
    $entity = $loaded_entity[$id];
    $label = entity_label($type, $entity);

    if ($label) {
      // Some characters are not supported in docId for Zanata, so these are
      // replaced with '_'.
      $label = strtr($label,
                ' ,.!?@+#$%^&*()[]{}<>`\'"\\|/;:~',
                '________________________________');

      // Leading underscores and dashes could cause the meaningful part of the
      // name to be lost if it is too long. Remove them.
      $label = ltrim($label, '_-');

      // Only use label if it has not all been trimmed away.
      if (strlen($label) > 0) {
        $doc_id = "{$type}/{$id}/{$label}";
        $doc_id
          = truncate_utf8($doc_id, TmgmtZanataConnector::MAX_DOCID_LENGTH, FALSE);
        // Zanata breaks if a doc id ends with _ or -.
        $doc_id = rtrim($doc_id, '_-');

        return $doc_id;
      }
    }

    // Assumption: this will never be as long as MAX_DOCID_LENGTH.
    return "{$type}/{$id}";
  }

  /**
   * Generate a REST URL for a given job item.
   *
   * @return string
   *   the REST URL for the given job item
   */
  public function getItemUrl(TMGMTJobItem $job_item) {
    $doc_id = $this->getDocId($job_item);
    // Zanata requires '/' to be replaced with ','.
    $encoded_doc_id = strtr($doc_id, '/', ',');
    return "{$this->getBaseUrl()}/{$encoded_doc_id}";
  }

  /**
   * Generate a REST URL for translations of the given job item.
   *
   * @return the REST URL for the translations of the given job item for the
   *         given locale.
   */
  public function getItemTranslationUrl(TMGMTJobItem $job_item) {
    return $this->getItemUrl($job_item) . '/translations/' . $this->getLocale() . '?ext=comment';
  }

  /**
   * Generate file upload URL.
   *
   * @return string
   *   the base REST URL to upload documents
   */
  public function getFileUploadUrl($doc_id) {
    return "{$this->serverUrl}/rest/file/source/{$this->projectId}/"
        . "{$this->versionId}?docId=" . $doc_id;
  }

  /**
   * Generate file download URL for documents.
   *
   * @return string
   *   the base REST URL to download documents
   */
  public function getFileDownloadUrl($doc_id) {
    return "{$this->serverUrl}/rest/file/translation/{$this->projectId}/"
        . "{$this->versionId}/" . $this->getLocale() . "/baked?docId=" . $doc_id;
  }

  /**
   * Generate list documents URL for project_version.
   *
   * @return string
   *   the base REST URL to list documents
   */
  public function getDocsListUrl() {
    return "{$this->serverUrl}/rest/projects/p/{$this->projectId}/"
        . "iterations/i/{$this->versionId}/r";
  }

  /**
   * Generate statistics URL for document, locale.
   *
   * @return string
   *   the base REST URL to query stats
   */
  public function getDocStatsUrl($doc_id) {
    return "{$this->serverUrl}/rest/stats/proj/{$this->projectId}/"
        . "iter/{$this->versionId}/doc/" . $doc_id . "?locale=" . $this->getLocale();
  }

  /**
   * Generate some common options for a HTTP request.
   *
   * @param string $content_type
   *   HTTP Header content-type.
   * @param string $accept
   *   HTTP Header accept.
   *
   * @return array
   *   options for a HTTP request that include authorization
   *   information and content type headers.
   */
  public function getBaseOptions($content_type = 'application/json', $accept = 'application/json') {
    return array(
      'headers' => array(
        'X-Auth-User' => $this->username,
        'X-Auth-Token' => $this->apiKey,
        'Content-Type' => $content_type,
        'Accept' => $accept,
      ),
    );
  }

  /**
   * Set headers and package $options['data'] ready to send on the file upload API.
   *
   * @return mixed[]
   *   a new options array with the relevant values set
   */
  public function prepareOptionsForFileUpload($options) {
    $boundary = md5(uniqid());

    // Modify header params.
    $options['method'] = 'POST';
    $options['headers']['Accept'] = "application/xml";
    $options['headers']['Content-Type'] = "multipart/form-data; charset=utf-8; boundary=$boundary";

    $post_data = array(
      'file' => $options['data'],
      'first' => 'true',
      'last' => 'true',
      'type' => 'HTML',
      'hash' => md5($options['data']),
    );

    $options['data'] = $this->multipartEncode($boundary, $post_data);
    return $options;
  }

  /**
   * Function to encode text data.
   *
   * @param string $name
   *   Name of text to encode.
   * @param string $value
   *   Text to encode.
   *
   * @return string
   *   Encoded text.
   */
  private function multipartEncText($name, $value) {
    return "Content-Disposition: form-data; name=\"$name\"\n\n$value\n";
  }

  /**
   * Function to multipart encode a html.
   *
   * @param string $contents
   *   The html contents to encode.
   *
   * @return string
   *   Encoded html.
   */
  private function multipartEncHtml($contents) {
    $mimetype = "text/html";
    $data = "Content-Disposition: form-data; name=\"file\" \n";
    // "file" key.
    $data .= "Content-Transfer-Encoding: UTF-8\n";
    $data .= "Content-Type: $mimetype\n\n";
    $data .= $contents . "\n";
    return $data;
  }

  /**
   * Base function to encode a data array.
   *
   * @param string $boundary
   *   Boundary value to separate parameters.
   * @param array $params
   *   Params to encode.
   *
   * @return string
   *   Encoded output.
   */
  private function multipartEncode($boundary, array $params) {
    $output = "";
    foreach ($params as $key => $value) {
      $output .= "--$boundary\r\n";
      if ($key == 'file') {
        $output .= $this->multipartEncHtml($value);
      }
      else {
        $output .= $this->multipartEncText($key, $value);
      }
    }
    $output .= "--$boundary--";
    return $output;
  }

  /**
   * Process a request, using proxy if specified.
   */
  public function processRequest($url, $options) {
    if ($this->proxy['enable']) {
      // Set both since chr will choose based on the request protocol.
      // Config could have either http or https server and the admin should
      // ensure the given proxy config works with what they have entered.
      $options['http_proxy'] = $this->proxy;
      $options['https_proxy'] = $this->proxy;
    }
    return chr_curl_http_request($url, $options);
  }

}
