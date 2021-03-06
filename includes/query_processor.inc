<?php

/**
 * @file
 * Contains methods to build and execute a solr query. Depends on
 * Apache_Solr_Php client.
 */

/**
 * Islandora Solr Query Processor.
 *
 * Used to store Solr query parameters and to connect to Solr to execute the
 * query. Populates the islandoraSolrResult property with the processed Solr
 * query results.
 */
class IslandoraSolrQueryProcessor {

  public $solrQuery;
  // Query alternative set if solrQuery is empty.
  public $internalSolrQuery;
  public $solrStart;
  public $solrLimit;
  public $solrDefType;
  // All other Solr parameters.
  public $solrParams = array();
  // Solr results tailored for Islandora's use.
  public $islandoraSolrResult;
  // The current display (for modules wanting to alter the query of a display).
  public $display;
  // Parameters from URL.
  public $internalSolrParams;
  public $differentKindsOfNothing = array(
    ' ',
    '%20',
    '%252F',
    '%2F',
    '%252F-',
    '',
  );

  protected $solrVersion;

  /**
   * Handle deprectation of old class member gracefully.
   */
  public function __get($name) {
    $map = array(
      'different_kinds_of_nothing' => 'differentKindsOfNothing',
    );

    if (isset($map[$name])) {
      $new_name = $map[$name];
      $trace = debug_backtrace();

      $message = t('Use of variable name "@class->@old_name" on line @line of @file deprecated as of version @version. Refactor to use "@class->@name" before the next release.', array(
        '@old_name' => $name,
        '@name' => $new_name,
        '@class' => __CLASS__,
        '@version' => '7.x-1.2',
        '@line' => $trace[0]['line'],
        '@file' => $trace[0]['file'],
      ));

      trigger_error($message, E_USER_DEPRECATED);

      return $this->$new_name;
    }
  }

  /**
   * IslandoraSolrQueryProcessor constructor.
   */
  public function __construct() {
    $this->solrVersion = islandora_solr_get_solr_version();
  }

  /**
   * Solr removed Date faceting version 6.
   *
   * @return bool
   *   Whether we know your Solr version and its below 6.
   */
  protected function solrHasDateFacets() {
    return ($this->solrVersion === FALSE
      || (isset($this->solrVersion['major'])
      && $this->solrVersion['major'] < 6));
  }

  /**
   * Build and execute a query.
   *
   * @param string $query
   *   The query string provided in the url.
   * @param array $params
   *   All URL parameters from the solr results page.
   * @param bool $alter_results
   *   Whether or not to send out hooks to alter the islandora_solr_results.
   */
  public function buildAndExecuteQuery($query, $params = NULL, $alter_results = TRUE) {
    // Set empty string.
    if (variable_get('islandora_solr_request_handler', '') == 'standard') {
      if (!$query || $query == ' ') {
        $query = '%252F';
      }
    }
    // Build the query and apply admin settings.
    $this->buildQuery($query, $params);

    // Execute the query.
    $this->executeQuery($alter_results);
  }

  /**
   * Builds Solr query.
   *
   * Build the query and performs checks based on URL parameters and
   * defaults set in the Islandora Solr admin form. Populates the properties to
   * be used for the query execution. Includes a module_invoke_all to make
   * changes to the query.
   *
   * @see IslandoraSolrQueryProcessor::buildAndExecuteQuery()
   *
   * @param string $query
   *   The query string provided in the URL.
   * @param array $params
   *   All URL parameters from the Solr results page.
   */
  public function buildQuery($query, $params = array()) {
    // Set internal parameters gathered from the URL but not 'q' and 'page'.
    $this->internalSolrParams = $params;
    unset($this->internalSolrParams['q']);
    unset($this->internalSolrParams['page']);

    // Set Solr type (dismax).
    if (isset($this->internalSolrParams['type']) && ($this->internalSolrParams['type'] == 'dismax' || $this->internalSolrParams['type'] == 'edismax')) {
      $this->solrDefType = $this->internalSolrParams['type'];
      $this->solrParams['defType'] = $this->internalSolrParams['type'];
    }

    // XXX: Fix the query as some characters will break the search : and / are
    // examples.
    $this->solrQuery = islandora_solr_restore_slashes(urldecode($query));

    // If the query is empty.
    if (empty($this->solrQuery) || in_array($this->solrQuery, $this->differentKindsOfNothing)) {
      // So we can allow empty queries to dismax.
      $this->solrQuery = ' ';
      // Set base query.
      $this->internalSolrQuery = variable_get('islandora_solr_base_query', '*:*');

      // We must also undo dismax if it has been set.
      $this->solrDefType = NULL;
      $this->solrParams['defType'] = NULL;
    }

    // Set sort.
    if (isset($this->internalSolrParams['sort'])) {
      // If multiple sorts are being passed they are expected to already be
      // an array with the values containing "thefield thesortorder".
      if (is_array($this->internalSolrParams['sort'])) {
        $this->solrParams['sort'] = $this->internalSolrParams['sort'];
      }
      else {
        $sort_explode = preg_split(
          ISLANDORA_SOLR_QUERY_SPLIT_REGEX,
          $this->internalSolrParams['sort']
        );
        // Check if an order is given and if the order value is 'asc' or 'desc'.
        if (isset($sort_explode[1]) && ($sort_explode[1] == 'asc' || $sort_explode[1] == 'desc')) {
          $this->solrParams['sort'] = $this->internalSolrParams['sort'];
        }
        else {
          // Use ascending.
          $this->solrParams['sort'] = $sort_explode[0] . ' asc';
        }
      }
    }
    else if ($this->solrQuery == ' ') {
      // if the search was empty, sort by title
      $this->solrParams['sort'] = 'title_sort asc';
    }
    else {
      $base_sort = variable_get('islandora_solr_base_sort', '');
      $base_sort = trim($base_sort);
      if (!empty($base_sort)) {
        $this->solrParams['sort'] = $base_sort;
      }
    }


    // Check if PID is part of the query - escape :
    //if (strstr($this->solrQuery, "PID:")) {
  if (preg_match('/^PID:\([a-zA-Z]+:[0-9a-zA-Z]+\)$/', $this->solrQuery)){
       $newQuery = substr($this->solrQuery,4);
       $newQuery = str_replace(':', '\:', $newQuery);
       $this->solrQuery = 'PID:' . $newQuery;
    }

    // Set display property (so display plugin modules can use this in a
    // conditional to alter the query class).
    if (isset($this->internalSolrParams['display'])) {
      $this->display = $this->internalSolrParams['display'];
    }
    else {
      $this->display = variable_get('islandora_solr_primary_display', 'default');
    }

    // Get pager variable.
    $start_page = isset($_GET['page']) ? $_GET['page'] : (isset($params['start']) ? $params['start'] : 0);

    // Set results limit.
    $this->solrLimit = isset($this->internalSolrParams['limit']) ? $this->internalSolrParams['limit'] : variable_get('islandora_solr_num_of_results', 20);

    // Set solr start.
    $this->solrStart = max(0, $start_page) * $this->solrLimit;

    // Set facet parameters.
    $facet_array = islandora_solr_get_fields('facet_fields', TRUE, FALSE, TRUE);
    $facet_fields = implode(",", array_keys($facet_array));

    // set params
    $params_array = array(
      'facet' => 'true',
      'facet.mincount' => variable_get('islandora_solr_facet_min_limit', '2'),
      'facet.limit' => variable_get('islandora_solr_facet_max_limit', '20'),
        // Comma separated list configured in the block config.
      'facet.field' => explode(',', $facet_fields),
      'spellcheck' => 'true',
    );

    $request_handler = variable_get('islandora_solr_request_handler', FALSE);
    if ($request_handler) {
      $params_array['qt'] = $request_handler;
    }

    // Check for date facets.
    $facet_dates_ranges = islandora_solr_get_range_facets();
    if (!empty($facet_dates_ranges)) {
      // Set range/date variables.
      $params_date_facets = array();
      $facet_dates = array_filter($facet_dates_ranges, function($o) {
        return islandora_solr_is_date_field($o['solr_field']);
      });
      foreach ($facet_dates_ranges as $key => $value) {
        $field = $value['solr_field'];
        $start = $value['solr_field_settings']['range_facet_start'];
        $end = $value['solr_field_settings']['range_facet_end'];
        $gap = $value['solr_field_settings']['range_facet_gap'];
        if ($this->solrHasDateFacets()) {
          // < Solr 6 or we don't know.
          $params_date_facets["facet.date"][] = $field;
          $params_date_facets["f.{$field}.facet.date.start"] = $start;
          // Custom field settings.
          if ($start) {
            $params_date_facets["f.{$field}.facet.date.start"] = $start;
          }
          if ($end) {
            $params_date_facets["f.{$field}.facet.date.end"] = $end;
          }
          if ($gap) {
            $params_date_facets["f.{$field}.facet.date.gap"] = $gap;
          }
          // Default settings.
          $params_date_facets["facet.date.start"] = 'NOW/YEAR-20YEARS';
          $params_date_facets["facet.date.end"] = 'NOW';
          $params_date_facets["facet.date.gap"] = '+1YEAR';
        }
        else {
          // No more date facets.
          $params_date_facets["facet.range"][] = $field;
          if (in_array($field, $facet_dates)) {
            // Use date defaults for date solr fields.
            // TODO: Maybe these should be removed and left to the config form.
            if (!$start) {
              $start = 'NOW/YEAR-20YEARS';
            }
            if (!$end) {
              $end = 'NOW';
            }
            if (!$gap) {
              $gap = '+1YEAR';
            }
          }
          if ($start) {
            $params_date_facets["f.{$field}.facet.range.start"] = $start;
          }
          if ($end) {
            $params_date_facets["f.{$field}.facet.range.end"] = $end;
          }
          if ($gap) {
            $params_date_facets["f.{$field}.facet.range.gap"] = $gap;
          }
        }
        // When the range slider is enabled we always want to return empty
        // values.
        if ($value['solr_field_settings']['range_facet_slider_enabled'] == 1) {
          $params_date_facets["f.{$field}.facet.mincount"] = 0;
        }
        // Remove range/date field from facet.field array.
        $pos = array_search($field, $params_array['facet.field']);
        unset($params_array['facet.field'][$pos]);
      }

      $params_array = array_merge($params_array, $params_date_facets);
    }

    // Determine the default facet sort order.
    $default_sort = (variable_get('islandora_solr_facet_max_limit', '20') <= 0 ? 'index' : 'count');

    $facet_sort_array = array();
    foreach ($facet_array as $key => $value) {
      if (isset($value['solr_field_settings']['sort_by']) && $value['solr_field_settings']['sort_by'] != $default_sort) {
        // If the sort doesn't match default then specify it in the parameters.
        $facet_sort_array["f.{$key}.facet.sort"] = check_plain($value['solr_field_settings']['sort_by']);
      }
    }
    $params_array = array_merge($params_array, $facet_sort_array);

    // Highlighting.
    $highlighting_array = islandora_solr_get_snippet_fields();
    if ((strstr($this->solrQuery, "catch_all_fulltext_mt") == NULL)&&(strstr($this->solrQuery, "catch_all_foxml_text_mt") == NULL)) {
        $highlighting_array = '';
    }
    if (!empty($highlighting_array)) {
      $highlights = implode(',', $highlighting_array);
      $highlighting_params = array(
        'hl' => isset($highlights) ? 'true' : NULL,
        'hl.fl' => isset($highlights) ? $highlights : NULL,
        'hl.snippets' => 1000,
        'hl.fragsize' => 400,
        'hl.maxAnalyzedChars' => 2000000,
        'hl.simple.pre' => '<span class="islandora-solr-highlight">',
        'hl.simple.post' => '</span>',
      );
      $params_array += $highlighting_params;
    }

    // Add parameters.
    $this->solrParams = array_merge($this->solrParams, $params_array);

    // Set base filters.
    $base_filters = preg_split("/\\r\\n|\\n|\\r/", variable_get('islandora_solr_base_filter', ''), -1, PREG_SPLIT_NO_EMPTY);

    // temporary fix to not enforce site filter for full text searches from PALMM with collection specified
    // child page objects still need to be reindexed to update their site information
    // when finished, remove this code and use site filter for all searches
    $site_root = variable_get('islandora_repository_pid', 'islandora:root');
    $fulltextSrch = ((strstr($this->solrQuery, "catch_all_fulltext_mt"))||(strstr($this->solrQuery, "catch_all_foxml_text_mt")));
    $fulltextFromPalmm = (($site_root == 'palmm:root')&&($fulltextSrch));
    if ((isset($this->internalSolrParams['collection']))&&($fulltextFromPalmm)) {
      $base_filters = preg_split("/\\r\\n|\\n|\\r/", '', -1, PREG_SPLIT_NO_EMPTY);
    }

    // Adds ability for modules to include facets which will not show up in
    // breadcrumb trail.
    if (isset($params['hidden_filter'])) {
      $base_filters = array_merge($base_filters, $params['hidden_filter']);
    }
    // Set filter parameters - both from url and admin settings.
    if (isset($this->internalSolrParams['f']) && is_array($this->internalSolrParams['f'])) {
      $this->solrParams['fq'] = $this->internalSolrParams['f'];
      if (!empty($base_filters)) {
        $this->solrParams['fq'] = array_merge($this->internalSolrParams['f'], $base_filters);
      }
    }
    elseif (!empty($base_filters)) {
      $this->solrParams['fq'] = $base_filters;
    }

    $fromAdvsrch = (!isset($this->internalSolrParams['type']) || ($this->internalSolrParams['type'] != 'dismax' && $this->internalSolrParams['type'] != 'edismax'));
    //$fulltextSrch = ((strstr($this->solrQuery, "catch_all_fulltext_mt"))||(strstr($this->solrQuery, "catch_all_foxml_text_mt")));

    // Restrict results based on collection
    //$site_root = variable_get('islandora_repository_pid', 'islandora:root');

    if ((isset($this->internalSolrParams['collection']))&&(($this->internalSolrParams['collection'] != $site_root)||($fulltextFromPalmm)))
    {
      $collection = $this->internalSolrParams['collection'];
      $collections_filter = '';
      $rc = $this->get_collection_search_filter($collection, $collections_filter);
/*
      $fedora_object = islandora_object_load(variable_get('islandora_repository_pid', 'islandora:root'));
      $query = <<<'EOQ'
SELECT ?subject ?content
FROM <#ri>
WHERE {
  ?subject <fedora-model:hasModel> ?content;
           <fedora-rels-ext:isMemberOfCollection>+ <info:fedora/!pid>;
           <fedora-model:state> <info:fedora/fedora-system:def/model#Active> .
FILTER(!contentfilter)
}
EOQ;
      $contentfilter = 'sameTerm(?content, <info:fedora/islandora:collectionCModel>)';
      // if non-empty search or advanced search, RI query includes newspaper issues and serial issues
      if (($this->solrQuery != ' ')||($fromAdvsrch)) {
         $contentfilter .= ' || sameTerm(?content, <info:fedora/islandora:newspaperCModel>) || sameTerm(?content, <info:fedora/islandora:rootSerialCModel>)';
      }
      //if (!isset($this->internalSolrParams['type']) || ($this->internalSolrParams['type'] != 'dismax' && $this->internalSolrParams['type'] != 'edismax')) {
      // if full text search, RI query includes books
      if ((strstr($this->solrQuery, "catch_all_fulltext_mt"))||(strstr($this->solrQuery, "catch_all_foxml_text_mt"))) {
         $contentfilter .= ' || sameTerm(?content, <info:fedora/islandora:bookCModel>)';
      }
      $formatted_query = format_string($query, array(
        '!pid' => $collection,
        '!contentfilter' => $contentfilter,
      ));
      $results = $fedora_object->repository->ri->sparqlQuery($formatted_query, 'unlimited');
      foreach ($results as $result) {
          if ($result['content']['value'] == 'islandora:collectionCModel') {
              $collections_filter = $collections_filter . 'RELS_EXT_isMemberOfCollection_uri_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
          }
          else if ($result['content']['value'] == 'islandora:newspaperCModel') {
            $collections_filter = $collections_filter . 'parent_newspaper_id_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
          }
          else if ($result['content']['value'] == 'islandora:bookCModel') {
              $collections_filter = $collections_filter . 'RELS_EXT_isMemberOf_uri_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
          }
          else if ($result['content']['value'] == 'islandora:rootSerialCModel') {
              $collections_filter = $collections_filter . 'parent_serial_id_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
          }
      }
*/
      $collections_filter = $collections_filter . 'RELS_EXT_isMemberOfCollection_uri_ms:info\:fedora/' . str_replace(':', '\:', $collection);
      $this->solrParams['fq'][] = trim($collections_filter);
    }

    // simple search filters
    if (isset($this->internalSolrParams['type']) && ($this->internalSolrParams['type'] == 'dismax' || $this->internalSolrParams['type'] == 'edismax')) {
      //$this->solrParams['fq'][] = '-RELS_EXT_isComponentOf_uri_ms:*';
      if ($this->solrQuery == ' ') {
        $this->solrParams['fq'][] = '-RELS_EXT_isMemberOf_uri_ms:*';
        $this->solrParams['fq'][] = '-RELS_EXT_isComponentOf_uri_ms:*';
      }
      else {
        $this->solrParams['fq'][] = '-RELS_EXT_hasModel_uri_ms:info\:fedora/islandora\:pageCModel';
        $this->solrParams['fq'][] = '-RELS_EXT_hasModel_uri_ms:info\:fedora/islandora\:newspaperPageCModel';
      }
    }
    else if ((strstr($this->solrQuery, "catch_all_foxml_text_mt") != NULL)||(strstr($this->solrQuery, "catch_all_fields_mt") != NULL)) {
      // remove serial section objects from advanced search - no mods
      $this->solrParams['fq'][] = '-RELS_EXT_hasModel_uri_ms:info\:fedora/islandora\:intermediateSerialCModelStub';
    }

    // Restrict results based on specified namespaces.
    $namespace_list = trim(variable_get('islandora_solr_namespace_restriction', ''));
    if ($namespace_list) {
      $namespaces = preg_split('/[,|\s]/', $namespace_list);
      $namespace_array = array();
      foreach (array_filter($namespaces) as $namespace) {
        $namespace_array[] = "PID:$namespace\:*";
      }
      $this->solrParams['fq'][] = implode(' OR ', $namespace_array);
    }

    if (isset($this->internalSolrParams['type']) && ($this->internalSolrParams['type'] == "dismax" || $this->internalSolrParams['type'] == "edismax")) {
      if (variable_get('islandora_solr_use_ui_qf', FALSE) || !islandora_solr_check_dismax()) {
        // Put our "qf" in if we are configured to, or we have none from the
        // request handler.
        $this->solrParams['qf'] = variable_get('islandora_solr_query_fields', 'dc.title^5 dc.subject^2 dc.description^2 dc.creator^2 dc.contributor^1 dc.type catch_all_fields_mt^1');
      }
    }

    // Invoke a hook for third-party modules to alter the parameters.
    // The hook implementation needs to specify that it takes a reference.
    module_invoke_all('islandora_solr_query', $this);
    drupal_alter('islandora_solr_query', $this);

    // Reset solrStart incase the number of results (ie. $this->solrLimit) is
    // modified.
    $this->solrStart = max(0, $start_page) * $this->solrLimit;
  }

  /**
   * Reset results.
   */
  public function resetResults() {
    unset($this->islandoraSolrResult);
  }

  /**
   * Connects to Solr and executes the query.
   *
   * Populates islandoraSolrResults property with the raw Solr results.
   *
   * @param bool $alter_results
   *   Whether or not to send out hooks to alter the islandora_solr_results.
   */
  public function executeQuery($alter_results = TRUE, $use_post = FALSE) {
    // Init Apache_Solr_Service object.
    $path_parts = parse_url(variable_get('islandora_solr_url', 'localhost:8080/solr'));
    $solr = new Apache_Solr_Service($path_parts['host'], $path_parts['port'], $path_parts['path'] . '/');
    $solr->setCreateDocuments(0);

    // Query is executed.
    try {
      $solr_query = ($this->internalSolrQuery) ? $this->internalSolrQuery : $this->solrQuery;
      $method = $use_post ? 'POST' : 'GET';
      $results = $solr->search($solr_query, $this->solrStart, $this->solrLimit, $this->solrParams, $method);
    }
    catch (Exception $e) {
      drupal_set_message(check_plain(t('Error searching Solr index')) . ' ' . $e->getMessage(), 'error');
    }

    $object_results = array();
    if (isset($results)) {
      $solr_results = json_decode($results->getRawResponse(), TRUE);
      // Invoke a hook for third-party modules to be notified of the result.
      module_invoke_all('islandora_solr_query_result', $solr_results);
      // Create results tailored for Islandora's use.
      $object_results = $solr_results['response']['docs'];
      $content_model_solr_field = variable_get('islandora_solr_content_model_field', 'RELS_EXT_hasModel_uri_ms');
      $datastream_field = variable_get('islandora_solr_datastream_id_field', 'fedora_datastreams_ms');
      $object_label = variable_get('islandora_solr_object_label_field', 'fgs_label_s');
      if (!empty($object_results)) {
        if (isset($this->internalSolrParams['islandora_solr_search_navigation']) && $this->internalSolrParams['islandora_solr_search_navigation']) {
          $id = bin2hex(drupal_random_bytes(10));
          $page_params = drupal_get_query_parameters();
          $search_nav_qp = $this;
          $search_nav_qp->islandoraSolrResult = NULL;
          $_SESSION['islandora_solr_search_nav_params'][$id] = array(
            'path' => current_path(),
            'query' => $this->solrQuery,
            'query_internal' => $this->internalSolrQuery,
            'limit' => $this->solrLimit,
            'params' => $this->solrParams,
            'params_internal' => $this->internalSolrParams,
          );

          $url_params = array(
            'solr_nav' => array(
              'id' => $id,
              'page' => (isset($page_params['page']) ? $page_params['page'] : 0),
            ));
        }
        else {
          $url_params = array();
        }

        foreach ($object_results as $object_index => $object_result) {
          unset($object_results[$object_index]);
          $object_results[$object_index]['solr_doc'] = $object_result;
          $pid = $object_results[$object_index]['solr_doc']['PID'];
          $object_results[$object_index]['PID'] = $pid;
          $object_results[$object_index]['object_url'] = 'islandora/object/' . $object_results[$object_index]['solr_doc']['PID'];
          if (isset($object_result[$content_model_solr_field])) {
            $object_results[$object_index]['content_models'] = $object_result[$content_model_solr_field];
          }
          if (isset($object_result[$datastream_field])) {
            $object_results[$object_index]['datastreams'] = $object_result[$datastream_field];
          }

          if (isset($object_result[$object_label])) {
            $object_label_value = $object_result[$object_label];
            $object_results[$object_index]['object_label'] = is_array($object_label_value) ? implode(", ", $object_label_value) : $object_label_value;
          }
          // reset to more useful information for book and newspaper page objects
          if (isset($object_result[$content_model_solr_field]) && ($object_result[$content_model_solr_field][0] == 'info:fedora/islandora:pageCModel')) {
              if (isset($object_result['RELS_EXT_isPageNumber_literal_ms']) && isset($object_result['RELS_EXT_isPageOf_uri_ms'])) {
                  $object_results[$object_index]['object_label'] = $this->getTitle($object_result['RELS_EXT_isPageOf_uri_ms'][0]) . ' (' . $object_result['RELS_EXT_isPageNumber_literal_ms'][0] . ')';
              }
          }
          if (isset($object_result[$content_model_solr_field]) && ($object_result[$content_model_solr_field][0] == 'info:fedora/islandora:newspaperPageCModel')) {
              if (isset($object_result['RELS_EXT_isPageNumber_literal_ms']) && isset($object_result['RELS_EXT_isPageOf_uri_ms'])) {
                  $object_results[$object_index]['object_label'] = $this->getTitle($object_result['RELS_EXT_isPageOf_uri_ms'][0]) . ' (PAGE ' . $object_result['RELS_EXT_isPageNumber_literal_ms'][0] . ')';
              }
          }
          if (!isset($object_result[$datastream_field]) || in_array('TN', $object_result[$datastream_field])) {
            // XXX: Would be good to have an access check on the TN here...
            // Doesn't seem to a nice way without loading the object, which
            // this methods seems to explicitly avoid doing...
            $object_results[$object_index]['thumbnail_url'] = $object_results[$object_index]['object_url'] . '/datastream/TN/view';
          }
          else {
            $object_results[$object_index]['thumbnail_url'] = drupal_get_path('module', 'islandora_solr') . '/images/defaultimg.png';
          }
          if (variable_get('islandora_solr_search_navigation', FALSE)) {
            $url_params['solr_nav']['offset'] = $object_index;
          }
          $object_results[$object_index]['object_url_params'] = $url_params;
          $object_results[$object_index]['thumbnail_url_params'] = $url_params;
        }

        // Allow other parts of code to modify the tailored results.
        if ($alter_results) {
          // Hook to alter based on content model.
          module_load_include('inc', 'islandora', 'includes/utilities');
          foreach ($object_results as $object_index => $object_result) {
            if (isset($object_result['content_models'])) {
              foreach ($object_result['content_models'] as $content_model_uri) {
                // Regex out the info:fedora/ from the content model.
                $cmodel_name = preg_replace('/info\:fedora\//', '', $content_model_uri, 1);
                $hook_list = islandora_build_hook_list('islandora_solr_object_result', array($cmodel_name));
                drupal_alter($hook_list, $object_results[$object_index], $this);
              }
            }
          }
          // Hook to alter everything.
          drupal_alter('islandora_solr_results', $object_results, $this);
          // Additional Solr doc preparation. Includes field permissions and
          // limitations.
          $object_results = $this->prepareSolrDoc($object_results);
        }
      }
      // Save results tailored for Islandora's use.
      unset($solr_results['response']['docs']);
      $solr_results['response']['objects'] = $object_results;
      $this->islandoraSolrResult = $solr_results;
    }
    else {
      $this->islandoraSolrResult = NULL;
    }
  }

  /**
   * Filter all Solr docs.
   *
   * Iterates of the Solr doc of every result object and applies filters
   * sort orders.
   *
   * @param array $object_results
   *   An array containing the prepared object results.
   *
   * @return array
   *   The object results array with updated solr doc values.
   */
  public function prepareSolrDoc($object_results) {
    // Optionally limit results to values given.
    $limit_results = variable_get('islandora_solr_limit_result_fields', 0);
    // Look for fields with no permission.
    $fields_all = islandora_solr_get_fields('result_fields', FALSE);
    $fields_filtered = islandora_solr_get_fields('result_fields');
    $fields_no_permission = array_diff($fields_all, $fields_filtered);

    module_load_include('inc', 'islandora_solr', 'includes/utilities');
    // Loop over object results.
    foreach ($object_results as $object_index => $object_result) {
      $doc = $object_result['solr_doc'];
      $pid = $object_result['PID'];
      $rows = array();
      // 1: Add defined fields.
      foreach ($fields_filtered as $field => $label) {
        if (isset($doc[$field]) && !empty($doc[$field])) {
          if ($field == 'OCR_t' || $field == 'FULL_TEXT_t' || $field == 'pdftext_t') {
              if ((strstr($this->solrQuery, "catch_all_fulltext_mt") == NULL)&&(strstr($this->solrQuery, "catch_all_foxml_text_mt") == NULL)) {
                  // do not set full text fields for display when not searching full text
                  $value = '';
              }
          }
          else if (($field == 'parent_book_id_ms')||($field == 'parent_issue_id_ms')||($field == 'parent_newspaper_id_ms')) {
              $value = $this->getTitle($doc[$field][0]);
          }
          else if ($field == 'RELS_EXT_hasModel_uri_ms') {
              $value = $this->getFormat($doc[$field][0]);
          }
          else if ($field == 'mods_accessCondition_use_and_reproduction_reuse_or_not_ms') {
              $value = $this->getReuseLabel($doc[$field][0]);
          }
          else if ($field == 'mods_accessCondition_use_and_reproduction_rightsstatements_org_ms') {
              $value = getRightsLabel($doc[$field][0]);
          }
          else if ($field == 'mods_accessCondition_use_and_reproduction_creative_commons_ms') {
              $value = getRightsLabel($doc[$field][0]);
          }
          else if ((($field == 'mods_titleInfo_title_ms')||($field == 'mods_titleInfo_title_mt'))&&(isset($fields_filtered['title_display_ms']))&&(isset($doc['title_display_ms']))&&(!empty($doc['title_display_ms']))) {
            $value = '';
          }
          else {
            $value = $doc[$field];
          }
          if (isset($value) && !empty($value))
            $rows[$field] = is_array($value) ? implode(", ", $value) : $value;
        }
        // add PURL field
        if (($field == 'mods_location_url_purl_ms')&&(isset($doc['mods_location_url_ms']))) {
          $value = array();
          foreach ($doc['mods_location_url_ms'] as $location_url) {
            if (strstr($location_url,'purl.')) {
              $value[] = $location_url;
            }
          }
          if (isset($value) && !empty($value))
            $rows[$field] = is_array($value) ? implode(", ", $value) : $value;
        }
      }
      // 2: If limit is not set, add other fields.
      if ($limit_results == 0) {
        foreach ($doc as $field => $value) {
          // Skip if added by the first loop already OR if no permission.
          if (isset($rows[$field]) || in_array($field, $fields_no_permission)) {
            continue;
          }
          $rows[$field] = $doc[$field];
        }
      }
      // Replace Solr doc rows.
      $object_results[$object_index]['solr_doc'] = $rows;
    }
    return $object_results;
  }

  function getTitle($object_pid) {
      $fedora_object = islandora_object_load(variable_get('islandora_repository_pid', 'islandora:root'));
      $query = <<<'EOQ'
SELECT ?title
FROM <#ri>
WHERE {
  <!pid> <fedora-model:label> ?title .
}
EOQ;
      $formatted_query = format_string($query, array(
        '!pid' => $object_pid,
      ));
      $results = $fedora_object->repository->ri->sparqlQuery($formatted_query, 'unlimited');
      foreach ($results as $result) {
          return $result['title']['value'];
      }
  }

  function getFormat($hasModel) {
    $format_array = array(
      'info:fedora/islandora:bookCModel' => 'E-book',
      'info:fedora/islandora:compoundCModel' => 'Set of related objects',
      'info:fedora/islandora:newspaperCModel' => 'Newspaper',
      'info:fedora/islandora:newspaperIssueCModel' => 'Newspaper issue',
      'info:fedora/islandora:newspaperPageCModel' => 'Newspaper page',
      'info:fedora/islandora:pageCModel' => 'E-book page',
      'info:fedora/islandora:sp-audioCModel' => 'Audio file',
      'info:fedora/islandora:sp_basic_image' => 'Image (JPEG)',
      'info:fedora/islandora:sp_large_image_cmodel' => 'Image (JPEG2000)',
      'info:fedora/islandora:sp_pdf' => 'Document (PDF)',
      'info:fedora/islandora:sp_videoCModel' => 'Video file',
      'info:fedora/islandora:binaryObjectCModel' => 'Downloadable file',
      'info:fedora/islandora:rootSerialCModel' => 'Serial',
      'info:fedora/islandora:intermediateCModel' => 'Serial component',
      'info:fedora/ir:citationCModel' => 'Citation',
      'info:fedora/ir:thesisCModel' => 'Thesis',
      'info:fedora/islandora:entityCModel' => 'Institutional Entity',
      'info:fedora/islandora:organizationCModel' => 'Institutional Organization',
      'info:fedora/islandora:personCModel' => 'Institutional Scholar',
      'info:fedora/islandora:eventCModel' => 'Institutional Event',
      'info:fedora/islandora:placeCModel' => 'Institutional Place',
      'info:fedora/islandora:comparativeEditionCModel' => 'Comparative Edition',
      'info:fedora/islandora:comparativeEditionPageCModel' => 'Comparative Edition Page',
      'info:fedora/islandora:comparativeEditionWitnessCModel' => 'Comparative Edition Witness',
    );

    if (isset($format_array[$hasModel])) {
        return $format_array[$hasModel];
    }

    return 'unknown';
  }

  function getReuseLabel($reusecode) {
    $reuse_array = array(
      'flvc_rights_free_reuse' => 'Free Re-use',
      'flvc_rights_limited_reuse' => 'Limited Re-use',
      'flvc_rights_no_reuse' => 'No Re-use',
    );

    if (isset($reuse_array[$reusecode])) {
        return $reuse_array[$reusecode];
    }

    return 'unknown';
  }

  function get_collection_search_filter($collection, &$collections_filter) {
    $results_count = 0;
    $fromAdvsrch = (!isset($this->internalSolrParams['type']) || ($this->internalSolrParams['type'] != 'dismax' && $this->internalSolrParams['type'] != 'edismax'));
    $fedora_object = islandora_object_load(variable_get('islandora_repository_pid', 'islandora:root'));
    $query = <<<'EOQ'
SELECT ?subject ?content
FROM <#ri>
WHERE {
  ?subject <fedora-model:hasModel> ?content;
           <fedora-rels-ext:isMemberOfCollection> <info:fedora/!pid>;
           <fedora-model:state> <info:fedora/fedora-system:def/model#Active> .
FILTER(!contentfilter)
}
EOQ;
    $contentfilter = 'sameTerm(?content, <info:fedora/islandora:collectionCModel>)';
    // if non-empty search or advanced search, RI query includes newspaper issues and serial issues
    if (($this->solrQuery != ' ')||($fromAdvsrch)) {
       $contentfilter .= ' || sameTerm(?content, <info:fedora/islandora:newspaperCModel>) || sameTerm(?content, <info:fedora/islandora:rootSerialCModel>)';
    }
    // if full text search, RI query includes books
    if ((strstr($this->solrQuery, "catch_all_fulltext_mt"))||(strstr($this->solrQuery, "catch_all_foxml_text_mt"))) {
      $contentfilter .= ' || sameTerm(?content, <info:fedora/islandora:bookCModel>)';
    }
    $formatted_query = format_string($query, array(
      '!pid' => $collection,
      '!contentfilter' => $contentfilter,
    ));
    $results = $fedora_object->repository->ri->sparqlQuery($formatted_query, 'unlimited');
    $results_count = count($results);
    foreach ($results as $result) {
      if ($result['content']['value'] == 'islandora:collectionCModel') {
        $collections_filter = $collections_filter . 'RELS_EXT_isMemberOfCollection_uri_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
        $rc = $this->get_collection_search_filter($result['subject']['value'], $collections_filter);
      }
      else if ($result['content']['value'] == 'islandora:newspaperCModel') {
        $collections_filter = $collections_filter . 'parent_newspaper_id_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
      }
      else if ($result['content']['value'] == 'islandora:bookCModel') {
        $collections_filter = $collections_filter . 'RELS_EXT_isMemberOf_uri_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
      }
      else if ($result['content']['value'] == 'islandora:rootSerialCModel') {
        $collections_filter = $collections_filter . 'parent_serial_id_ms:info\:fedora/' . str_replace(':', '\:', $result['subject']['value']) . ' OR ';
      }
    }
    return $results_count;
  }
}
