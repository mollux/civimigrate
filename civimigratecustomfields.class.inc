<?php

/**
 *
 */
class CiviMigrateCustomFields extends Civimigration {
  protected $debug = 0; // Set to 1 for debug info.
  protected $json_folder = NULL;
  protected $customFields = array();
  protected $entity = 'custom_field';

  public function __construct($arguments) {
    parent::__construct($arguments);
    $this->description = t('Create custom fields.');

    // This can also be an URL instead of a file path but we are assuming it shipe with the migrate module
    $json_folder = $this->json_folder;
    if(empty($json_folder)){
      throw new MigrateException('json folder must be defined');
    }
    $list_url = $json_folder . 'list.json';
    $item_url = $json_folder . '/customfields.json';
    $http_options = array();
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'fieldname' => array(
          'type' => 'varchar',
          'length' => 20,
          'not null' => TRUE,
        ),
      ),
      MigrateDestinationCivicrmApi::getKeySchema()
    );
    $this->source = new MigrateSourceList(
      new CiviMigrateListJSON($item_url, 'fieldname'),
      new CiviMigrateFileItemJSON($item_url, $http_options, 'fieldname'),
      $this->fields()
    );
    $this->addFieldMapping('title', 'title');
    $this->addFieldMapping('label', 'label');
    $this->addFieldMapping('is_active')->defaultValue(1);
    $this->addFieldMapping('data_type', 'data_type')->defaultValue('String');
    $this->addFieldMapping('html_type', 'html_type')->defaultValue('Text');
  }
  /**
   * Return the fields (this is cleaner than passing in the array in the MigrateSourceList class above)
   * @return array
   */
  function fields() {
    return array(
      'title' => 'The title of the content',
      'fieldname' => t('fieldname'),
      'title' => t('title'),
      'label' => t('Label'),
      'data_type' => t('data type'),
      'html_type' => t('html_type'),
    );
  }
  /**
   * (non-PHPdoc)
   * @see Civimigration::prepare()
   */
  function prepare(&$entity, $row){
    if(empty($entity->custom_group_id)){
      if(isset($row->custom_group->extends)){
        $extends = $row->custom_group->extends;
      }
      else{
        $extends = 'Contact';
      }
      $entity->custom_group_id = $this->getCustomGroupID($row->custom_group->title, $extends);
    }
    if(empty($entity->id)){
      $entity->id = $this->getCustomFieldID($entity);
    }
    if(!empty($row->options) && $row->options->type == 'shared'){
      // we are sharing with another field
      $entity->option_group_id =  civicrm_api('custom_field', 'getvalue', array(
        'version' => 3,
        'name' => $row->options->fieldname,
        'return' => 'option_group_id',
      ));
    }
  }

  /**
   *
   * @param object $entity
   * @param stdClass $row
   */
  function complete($entity, $row){
    if(isset($row->options)){
      $optionSpec = $row->options;
    }
    else{
      return;
    }
    if($optionSpec->type == 'query'){
      $options = $this->getFieldOptions($optionSpec->table, $optionSpec->key, $optionSpec->label);
      $this->createCustomFieldOptions($entity['id'], $options);
    }
  }

  /**
   * Here we establish all our custom fields
   * (non-PHPdoc)
   * @see Migration::preImport()
   */
  public function postImport() {
    parent::postImport();
    if(!civicrm_initialize()){
      return;
    }
    // need to clear the cache once the fields are created
    civicrm_api('system', 'flush', array('version' => 3));
  }

  /**
   * Get or create relevant group id
   * @param integer $groupTitle
   * @param string $extends
   * @throws MigrateException
   * @return integer groupID
   */
  function getCustomGroupID($groupTitle, $extends = 'Contact'){
    $group = civicrm_api('custom_group', 'get', array(
        'version' => 3,
        'title' => $groupTitle,
        'return' => 'id',
      )
    );
    if(!empty($group['id'])){
      //make sure it's active
      civicrm_api('custom_group', 'create', array(
        'version' => 3,
        'id' => $group['id'],
        'is_active' => 1,
      ));
      return $group['id'];
    }
    $group = civicrm_api('custom_group', 'create', array(
        'version' => 3,
        'title' => $groupTitle,
        'extends' => $extends,
        'is_active' => 1,
        'style' => 'Inline',
      )
    );
    if($group['is_error']){
      throw new MigrateException("failed to create $groupTitle extending $extends : " . $group['error_message']);
    }
    return $group['id'];
  }

  /**
   * Get / create custom field
   * @param integer $customGroupID
   * @param string $field  - this will be the key of a field in $this->customFields
   */
  function getCustomFieldID($entity){

    $customField = civicrm_api('custom_field', 'get', array(
        'version' => 3,
        'custom_group_id' => $entity->custom_group_id,
        'label' => $entity->label,
      )
    );

    if(!empty($customField['id'])){
      return $customField['id'];
    }

    return NULL;

  }

  /**
   * Get required options set
   * @param string $field  - this will be the key of a field in $this->customFields
   */
  function getFieldOptions($table, $keyField, $labelField){
    $query = db_select($table, 'tbl');
    $query->addField('tbl', $keyField, 'key_field');
    $query->addField('tbl', $labelField, 'label');
    return $query->execute()->fetchAllKeyed();
  }
  /**
   *  check / create options for field
   * @param integer $fieldID
   * @param array $options
   */
  function createCustomFieldOptions($fieldID, $options){
    $optionGroupID = civicrm_api('custom_field', 'getvalue', array(
      'version' => 3,
      'id' => $fieldID,
      'return' => 'option_group_id',
    ));

    if(!is_numeric($optionGroupID)){
      throw new MigrateException('no option group id for field' . $fieldID);
    }

    foreach ($options as $key => $label){
      $exists = civicrm_api('option_value', 'getcount', array(
          'version' => 3,
          'option_group_id' => $optionGroupID,
          'value' => $key
        )
      );
      if(!$exists){
        $optionValue = civicrm_api('option_value', 'create', array(
            'version' => 3,
            'option_group_id' => $optionGroupID,
            'value' => $key,
            'label' => $label,
          )
        );
        if($optionValue['is_error']){
          throw new MigrateException("could not create option value $key : $label for option group $optionGroupID for field $fieldID");
        }
      }
    }
  }
}

