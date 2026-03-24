class AlchemyHelperUI extends MovieClip
{
   var Menu;
   var button;
   var keycode;
   var navPanel;
   var _syncingGhosts;
   function AlchemyHelperUI()
   {
      super();
   }
   function onLoad()
   {
      this.Menu = this._parent._parent;
      if(this.Menu == undefined || this.Menu._subtypeName !== "Alchemy")
      {
         return undefined;
      }
      this.navPanel = this.Menu.navPanel;
      this.keycode = parseInt(this._parent._name.substr(16));
      this.installButtonHook();
      this.installListHooks();
      this.addButton();
   }
   function installButtonHook()
   {
      var _this = this;
      if(this.Menu.arv__UpdateButtonTextPatched == true)
      {
         return undefined;
      }
      this.Menu.arv__UpdateButtonTextPatched = true;
      this.Menu.sh__UpdateButtonText = this.Menu.UpdateButtonText;
      this.Menu.UpdateButtonText = function()
      {
         _this.Menu.sh__UpdateButtonText();
         _this.addButton();
      };
   }
   function installListHooks()
   {
      var _this = this;
      if(this.Menu.CategoryList != undefined && this.Menu.CategoryList.arv__InvalidateListDataPatched != true)
      {
         this.Menu.CategoryList.arv__InvalidateListDataPatched = true;
         this.Menu.CategoryList.sh__InvalidateListData = this.Menu.CategoryList.InvalidateListData;
         this.Menu.CategoryList.InvalidateListData = function()
         {
            _this.syncGhostEntries();
            this.sh__InvalidateListData();
         };
      }
      if(this.Menu.ItemList != undefined && this.Menu.ItemList.arv__InvalidateDataPatched != true)
      {
         this.Menu.ItemList.arv__InvalidateDataPatched = true;
         this.Menu.ItemList.sh__InvalidateData = this.Menu.ItemList.InvalidateData;
         this.Menu.ItemList.InvalidateData = function()
         {
            _this.syncGhostEntries();
            this.sh__InvalidateData();
         };
      }
      if(this.Menu.CategoryList != undefined && this.Menu.CategoryList.arv__ShowItemsListPatched != true)
      {
         this.Menu.CategoryList.arv__ShowItemsListPatched = true;
         this.Menu.CategoryList.sh__showItemsList = this.Menu.CategoryList.showItemsList;
         this.Menu.CategoryList.showItemsList = function()
         {
            this.sh__showItemsList();
            _this.syncGhostEntries();
            if(this.itemList != undefined && this.itemList.UpdateList != undefined)
            {
               this.itemList.UpdateList();
            }
         };
      }
      if(this.Menu.arv__GhostSelectPatched == true)
      {
         return undefined;
      }
      this.Menu.arv__GhostSelectPatched = true;
      this.Menu.sh__onItemSelect = this.Menu.onItemSelect;
      this.Menu.onItemSelect = function(event)
      {
         if(_this.handleGhostItemSelect())
         {
            return undefined;
         }
         return _this.Menu.sh__onItemSelect(event);
      };
      this.Menu.sh__onItemHighlightChange = this.Menu.onItemHighlightChange;
      this.Menu.onItemHighlightChange = function(event)
      {
         if(_this.handleGhostItemHighlight(event))
         {
            return undefined;
         }
         return _this.Menu.sh__onItemHighlightChange(event);
      };
      this.Menu.sh__onItemListPressed = this.Menu.onItemListPressed;
      this.Menu.onItemListPressed = function(event)
      {
         if(_this.handleGhostItemPress(event))
         {
            return undefined;
         }
         return _this.Menu.sh__onItemListPressed(event);
      };
   }
   function addButton()
   {
      this.button = this.navPanel.addButton({text:"Toggle Recipe View",controls:{keyCode:this.keycode}});
      this.navPanel.updateButtons(true);
   }
   function getArvState()
   {
      return _root.arv;
   }
   function getGhostEntries()
   {
      var arv = this.getArvState();
      if(arv == undefined || arv.enabled != true || arv.ghostIngredients == undefined)
      {
         return [];
      }
      return arv.ghostIngredients;
   }
   function getItemList()
   {
      return this.Menu.ItemList;
   }
   function getEntryList()
   {
      var itemList = this.getItemList();
      if(itemList == undefined)
      {
         return undefined;
      }
      return itemList.entryList;
   }
   function getActiveCategoryEntry()
   {
      if(this.Menu == undefined || this.Menu.CategoryList == undefined || this.Menu.CategoryList.CategoriesList == undefined)
      {
         return undefined;
      }
      return this.Menu.CategoryList.CategoriesList.selectedEntry;
   }
   function normalizeCategoryText(aText)
   {
      if(aText == undefined)
      {
         return "";
      }
      var text = String(aText);
      text = text.split("[")[0];
      text = text.split("$").join("");
      text = text.split("\r").join("");
      text = text.split("\n").join("");
      text = text.split("\t").join("");
      text = text.split(" ").join("");
      return text.toUpperCase();
   }
   function isIngredientCategory(aCategory)
   {
      if(aCategory == undefined)
      {
         return true;
      }
      if(aCategory.iconLabel == "ingredients")
      {
         return true;
      }
      var normalized = this.normalizeCategoryText(aCategory.text);
      return normalized == "" || normalized == "INGREDIENTS" || normalized == "ALL";
   }
   function ghostHasEffectName(aGhost, aCategoryName)
   {
      if(aGhost == undefined || aCategoryName == "")
      {
         return false;
      }
      var effectCount = aGhost.numItemEffects != undefined ? Number(aGhost.numItemEffects) : 4;
      var i = 0;
      while(i < effectCount)
      {
         var effectText = aGhost["itemEffect" + i];
         if(this.normalizeCategoryText(effectText) == aCategoryName)
         {
            return true;
         }
         i = i + 1;
      }
      return false;
   }
   function ghostMatchesCategory(aGhost, aCategory)
   {
      if(aGhost == undefined)
      {
         return false;
      }
      if(this.isIngredientCategory(aCategory))
      {
         return true;
      }
      var categoryName = this.normalizeCategoryText(aCategory.text);
      if(categoryName == "")
      {
         return true;
      }
      if(categoryName == "BENEFICIAL")
      {
         return aGhost.iconLabel == "beneficial" || aGhost.goodEffects != undefined;
      }
      if(categoryName == "HARMFUL")
      {
         return aGhost.iconLabel == "harmful" || aGhost.badEffects != undefined;
      }
      if(categoryName == "OTHER")
      {
         return aGhost.iconLabel == "other" || aGhost.otherEffects != undefined;
      }
      return this.ghostHasEffectName(aGhost,categoryName);
   }
   function syncGhostEntries()
   {
      if(this._syncingGhosts == true)
      {
         return undefined;
      }
      var entryList = this.getEntryList();
      if(entryList == undefined)
      {
         return undefined;
      }
      this._syncingGhosts = true;
      var arv = this.getArvState();
      var ghostByFormId = arv != undefined ? arv.ghostByFormId : undefined;
      var writePos = 0;
      var i = 0;
      var entry;
      while(i < entryList.length)
      {
         entry = entryList[i];
         if(entry == undefined || entry._arvInjected != true)
         {
            entryList[writePos] = entry;
            writePos = writePos + 1;
         }
         i = i + 1;
      }
      entryList.length = writePos;
      if(ghostByFormId != undefined)
      {
         i = 0;
         while(i < entryList.length)
         {
            entry = entryList[i];
            if(entry != undefined && entry.formId != undefined)
            {
               var ghostData = ghostByFormId[String(entry.formId)];
               if(ghostData != undefined)
               {
                  entry.isARVGhost = true;
                  entry.isGhost = true;
                  if(ghostData.iconColor != undefined)
                  {
                     entry.iconColor = ghostData.iconColor;
                  }
                  if(ghostData.iconLabel != undefined)
                  {
                     entry.iconLabel = ghostData.iconLabel;
                  }
                  entry.recipeCandidates = ghostData.recipeCandidates;
                  entry.recipeCount = ghostData.recipeCount;
                  entry.recipeDescription = ghostData.recipeDescription;
                  entry.goodEffects = ghostData.goodEffects;
                  entry.badEffects = ghostData.badEffects;
                  entry.otherEffects = ghostData.otherEffects;
               }
            }
            i = i + 1;
         }
      }
      var ghosts = this.getGhostEntries();
      var activeCategory = this.getActiveCategoryEntry();
      i = 0;
      while(i < ghosts.length)
      {
         entry = ghosts[i];
         if(entry != undefined && this.ghostMatchesCategory(entry,activeCategory) && this.findEntryIndexByFormId(entryList, entry.formId) == -1)
         {
            var newEntry = this.createGhostEntry(entry);
            newEntry._arvInjected = true;
            entryList.push(newEntry);
         }
         i = i + 1;
      }
      this._syncingGhosts = false;
   }
   function createGhostEntry(aGhost)
   {
      var entry = {};
      for(var key in aGhost)
      {
         entry[key] = aGhost[key];
      }
      entry.text = aGhost.displayName != undefined ? aGhost.displayName : aGhost.text;
      entry.displayName = entry.text;
      if(entry.filterFlag == undefined)
      {
         entry.filterFlag = 1;
      }
      entry.count = 0;
      entry.enabled = true;
      entry.isGhost = true;
      entry.isARVGhost = true;
      entry.skyui_itemDataProcessed = false;
      delete entry.customFilterFlag;
      return entry;
   }
   function findEntryIndexByFormId(aEntryList, aFormId)
   {
      var i = 0;
      while(i < aEntryList.length)
      {
         var entry = aEntryList[i];
         if(entry != undefined && entry.formId == aFormId)
         {
            return i;
         }
         i = i + 1;
      }
      return -1;
   }
   function getSelectedEntry()
   {
      var itemList = this.getItemList();
      if(itemList == undefined)
      {
         return undefined;
      }
      return itemList.selectedEntry;
   }
   function getSelectedIndex()
   {
      var itemList = this.getItemList();
      if(itemList == undefined)
      {
         return -1;
      }
      return itemList.selectedIndex;
   }
   function isGhostEntry(aEntry)
   {
      if(aEntry == undefined)
      {
         return false;
      }
      if(aEntry.isARVGhost == true)
      {
         return true;
      }
      var arv = this.getArvState();
      if(arv != undefined && arv.ghostByFormId != undefined && aEntry.formId != undefined)
      {
         return arv.ghostByFormId[String(aEntry.formId)] != undefined;
      }
      return false;
   }
   function getSelectedGhostState()
   {
      var arv = this.getArvState();
      if(arv == undefined || arv.selectedGhost == undefined || arv.selectedGhostFormId == undefined || arv.selectedGhostFormId == 0)
      {
         return undefined;
      }
      return arv.selectedGhost;
   }
   function resolveGhostDetails(aEntry)
   {
      var selectedGhost = this.getSelectedGhostState();
      if(selectedGhost != undefined && aEntry != undefined && selectedGhost.formId == aEntry.formId)
      {
         return selectedGhost;
      }
      return aEntry;
   }
   function notifyGhostSelected(aEntry)
   {
      if(aEntry == undefined)
      {
         return undefined;
      }
      if(_root.arv != undefined && _root.arv.onGhostSelected != undefined)
      {
         _root.arv.onGhostSelected(aEntry.formId);
      }
   }
   function clearGhostAdditionalDescription()
   {
      if(this.Menu == undefined || this.Menu.UpdateIngredients == undefined)
      {
         return undefined;
      }
      this.Menu.UpdateIngredients("",[],false);
   }
   function showGhostDetails(aEntry, aIndex)
   {
      var details = this.resolveGhostDetails(aEntry);
      if(details == undefined)
      {
         return undefined;
      }
      if(aIndex == undefined)
      {
         aIndex = this.getSelectedIndex();
      }
      if(this.Menu.ItemList != undefined)
      {
         this.Menu.ItemList.selectedIndex = aIndex;
      }
      this.Menu.FadeInfoCard(false);
      if(this.Menu.ItemInfo != undefined)
      {
         this.Menu.ItemInfo.itemInfo = details;
      }
      this.clearGhostAdditionalDescription();
      this.Menu.UpdateButtonText();
      gfx.io.GameDelegate.call("ShowItem3D",[false]);
   }
   function handleGhostItemHighlight(event)
   {
      var entry = this.getSelectedEntry();
      if(!this.isGhostEntry(entry))
      {
         return false;
      }
      this.notifyGhostSelected(entry);
      this.showGhostDetails(entry,event != undefined ? event.index : this.getSelectedIndex());
      return true;
   }
   function handleGhostItemSelect()
   {
      var entry = this.getSelectedEntry();
      if(!this.isGhostEntry(entry))
      {
         return false;
      }
      this.notifyGhostSelected(entry);
      this.showGhostDetails(entry);
      return true;
   }
   function handleGhostItemPress(event)
   {
      var entry = this.getSelectedEntry();
      if(!this.isGhostEntry(entry))
      {
         return false;
      }
      this.notifyGhostSelected(entry);
      this.showGhostDetails(entry,event != undefined ? event.index : this.getSelectedIndex());
      return true;
   }
}
