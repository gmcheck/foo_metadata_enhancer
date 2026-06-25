#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Text Utilities
Common text processing and cleaning utilities
"""

from typing import Any, Dict, List, Optional, Set, Union

INVALID_VALUES: Set[str] = {"n/a", "na", "none", "unknown", "null", "undefined", "-", "--", "---"}


def clean_value(value: Any, invalid_values: Optional[Set[str]] = None) -> str:
    """Clean a single value, replacing invalid values with empty string
    
    Args:
        value: The value to clean
        invalid_values: Custom set of invalid values (uses default if None)
    
    Returns:
        str: Cleaned value or empty string
    """
    if value is None:
        return ""
    
    if not isinstance(value, str):
        value = str(value)
    
    cleaned = value.strip()
    invalid = invalid_values or INVALID_VALUES
    
    if cleaned.lower() in invalid:
        return ""
    
    return cleaned


def clean_dict_values(data: Dict[str, Any], 
                       invalid_values: Optional[Set[str]] = None,
                       recursive: bool = True) -> Dict[str, Any]:
    """Clean all string values in a dictionary
    
    Args:
        data: Dictionary to clean
        invalid_values: Custom set of invalid values (uses default if None)
        recursive: Whether to recursively clean nested dicts and lists
    
    Returns:
        Dict[str, Any]: Dictionary with cleaned values
    """
    if not isinstance(data, dict):
        return data
    
    result = {}
    invalid = invalid_values or INVALID_VALUES
    
    for key, value in data.items():
        if isinstance(value, str):
            result[key] = clean_value(value, invalid)
        elif isinstance(value, dict) and recursive:
            result[key] = clean_dict_values(value, invalid, recursive)
        elif isinstance(value, list) and recursive:
            result[key] = clean_list_values(value, invalid, recursive)
        else:
            result[key] = value
    
    return result


def clean_list_values(data: List[Any], 
                       invalid_values: Optional[Set[str]] = None,
                       recursive: bool = True) -> List[Any]:
    """Clean all string values in a list
    
    Args:
        data: List to clean
        invalid_values: Custom set of invalid values (uses default if None)
        recursive: Whether to recursively clean nested dicts and lists
    
    Returns:
        List[Any]: List with cleaned values
    """
    if not isinstance(data, list):
        return data
    
    result = []
    invalid = invalid_values or INVALID_VALUES
    
    for item in data:
        if isinstance(item, str):
            result.append(clean_value(item, invalid))
        elif isinstance(item, dict) and recursive:
            result.append(clean_dict_values(item, invalid, recursive))
        elif isinstance(item, list) and recursive:
            result.append(clean_list_values(item, invalid, recursive))
        else:
            result.append(item)
    
    return result


def clean_field_dict(field_data: Dict[str, Any], 
                      value_key: str = "value",
                      invalid_values: Optional[Set[str]] = None) -> Dict[str, Any]:
    """Clean a field dictionary with nested 'value' key
    
    Common pattern in AI responses: {"value": "some value", "confidence": 0.9}
    
    Args:
        field_data: Field dictionary to clean
        value_key: Key name for the value field (default: "value")
        invalid_values: Custom set of invalid values (uses default if None)
    
    Returns:
        Dict[str, Any]: Field dictionary with cleaned value
    """
    if not isinstance(field_data, dict):
        return field_data
    
    result = dict(field_data)
    
    if value_key in result and isinstance(result[value_key], str):
        result[value_key] = clean_value(result[value_key], invalid_values)
    
    return result


def is_valid_value(value: Any, invalid_values: Optional[Set[str]] = None) -> bool:
    """Check if a value is valid (not empty and not in invalid set)
    
    Args:
        value: Value to check
        invalid_values: Custom set of invalid values (uses default if None)
    
    Returns:
        bool: True if value is valid
    """
    if value is None:
        return False
    
    if isinstance(value, str):
        cleaned = value.strip()
        invalid = invalid_values or INVALID_VALUES
        return bool(cleaned) and cleaned.lower() not in invalid
    
    return True
