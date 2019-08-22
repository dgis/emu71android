package org.emulator.seventy.one;

import android.app.Dialog;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Spinner;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatDialogFragment;
import androidx.appcompat.widget.Toolbar;
import androidx.constraintlayout.widget.ConstraintLayout;
import androidx.recyclerview.widget.ListAdapter;
import androidx.recyclerview.widget.RecyclerView;

import org.emulator.calculator.Utils;

import java.util.ArrayList;


public class PortSettingsFragment extends AppCompatDialogFragment {
    private static final String TAG = "PortSettings";
    private boolean debug = false;

    public PortSettingsFragment() {
    }

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setStyle(AppCompatDialogFragment.STYLE_NO_FRAME, android.R.style.Theme_Material);
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        Window window = dialog.getWindow();
        if(window != null)
            window.requestFeature(Window.FEATURE_NO_TITLE);
        return dialog;
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {

        String title = getString(R.string.dialog_port_configuration_title);
        Dialog dialog = getDialog();
        if(dialog != null)
            dialog.setTitle(title);

        // Inflate the layout for this fragment
        View view = inflater.inflate(R.layout.fragment_port_settings, container, false);

        Toolbar toolbar = view.findViewById(R.id.my_toolbar);
        toolbar.setTitle(title);
        toolbar.setNavigationIcon(R.drawable.ic_keyboard_backspace_white_24dp);
        toolbar.setNavigationOnClickListener(
                v -> dismiss()
        );

        ConstraintLayout constraintLayout = view.findViewById(R.id.constraintLayout);

        Spinner spinnerSelPort = view.findViewById(R.id.spinnerSelPort);
        spinnerSelPort.setSelection(1);
        spinnerSelPort.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {

            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        });

        ArrayList<String> listItems = new ArrayList<>();
        ArrayAdapter<String> adapterListViewPortData;
        adapterListViewPortData = new ArrayAdapter<>(getContext(), R.layout.simple_list_item_1, listItems);
        ListView listViewPortData = view.findViewById(R.id.listViewPortData);
        listViewPortData.setAdapter(adapterListViewPortData);
        listViewPortData.setOnItemClickListener((parent, view1, position, id) -> {

        });

        Button buttonAdd = view.findViewById(R.id.buttonAdd);
        buttonAdd.setOnClickListener(v -> {
            adapterListViewPortData.add("Hello " + adapterListViewPortData.getCount());
            Utils.setListViewHeightBasedOnChildren(listViewPortData);
        });

        Button buttonAbort = view.findViewById(R.id.buttonAbort);
        buttonAbort.setOnClickListener(v -> {

        });

        Button buttonDelete = view.findViewById(R.id.buttonDelete);
        buttonDelete.setOnClickListener(v -> {
            listItems.remove(0);
            adapterListViewPortData.notifyDataSetChanged();
            Utils.setListViewHeightBasedOnChildren(listViewPortData);
        });

        Button buttonApply = view.findViewById(R.id.buttonApply);
        buttonApply.setOnClickListener(v -> {

        });

        Spinner spinnerType = view.findViewById(R.id.spinnerType);
        spinnerType.setSelection(0);
        spinnerType.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {

            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {

            }
        });

        Spinner spinnerSize = view.findViewById(R.id.spinnerSize);
        spinnerSize.setSelection(7);

        Spinner spinnerChips = view.findViewById(R.id.spinnerChips);
        spinnerChips.setSelection(0);

        EditText editTextFile = view.findViewById(R.id.editTextFile);

        Button buttonBrowse = view.findViewById(R.id.buttonBrowse);
        buttonBrowse.setOnClickListener(v -> {

        });

        Spinner spinnerHardAddr = view.findViewById(R.id.spinnerHardAddr);
        spinnerHardAddr.setSelection(1);

        Button buttonTCPIP = view.findViewById(R.id.buttonTCPIP);
        buttonTCPIP.setOnClickListener(v -> {

        });

        return view;
    }
}
